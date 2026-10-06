/*
 * ESP32-S3-Cam 的 ISBN 条码(EAN-13)识别固件。
 *
 * 编译环境: Arduino IDE 2.2.1 + 厂家提供的 ESP32 包(esp32 2.0.11)。
 * 板型选择: 按厂家教程配置 ESP32S3-Cam(默认 ESP32S3 DEV 或厂家自定义板)。
 *
 * 功能:
 *   - 持续采集灰度图，识别 EAN-13 条码，校验位验证通过后去抖(连续 2 帧相同)。
 *   - 作为 I2C 从机(地址 0x52, SDA=47, SCL=48, 100kHz)，
 *     把结果通过寄存器协议交给 STM32 主机读取:
 *       0x00 STATUS  读: 0=空闲 1=有结果; 写 0x00 清除
 *       0x01 LEN     结果字节数
 *       0x02 TYPE    0x01=ISBN
 *       0x03 DATA    结果字节(LEN 个)
 *   - 去重: 同一结果只置位一次 STATUS；条码移开 500ms 后允许再次上报。
 *
 * 部署到机器人(仅 5V 供电、无 USB 主机)时，Serial 打印会自动跳过(if (Serial))。
 */

#include <Wire.h>
#include <string.h>
#include <string>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "camera_pins.h"
#include "ReadBarcode.h" /* zxing-cpp v2.2.1 (1D-only) */

#define I2C_SLAVE_ADDR 0x52
#define I2C_SDA_PIN 47
#define I2C_SCL_PIN 48
#define I2C_FREQ 100000

#define REG_STATUS 0x00
#define REG_LEN 0x01
#define REG_TYPE 0x02
#define REG_DATA 0x03

#define STABLE_FRAMES 1
#define REARM_MS 500

#define CAM_W 640
#define CAM_H 480

#define AP_SSID "ESP32S3-Cam"
#define AP_PASS "12345678"

static WebServer server(80);

static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

static volatile uint8_t g_status = 0;
static volatile uint8_t g_len = 0;
static volatile uint8_t g_type = 0;
static volatile uint8_t g_data[16];
static volatile uint8_t g_reg_sel = 0;

static uint8_t *g_gray = NULL;

static void i2c_on_receive(int len)
{
    uint8_t reg = 0;
    if (Wire.available()) {
        reg = (uint8_t)Wire.read();
    }
    g_reg_sel = reg;

    /* Only a 2-byte write [0x00, 0x00] clears STATUS. A 1-byte write [reg]
     * just selects the register for the following read, and must NOT clear. */
    if (len >= 2 && Wire.available()) {
        uint8_t val = (uint8_t)Wire.read();
        while (Wire.available()) {
            Wire.read();
        }
        if (reg == REG_STATUS && val == 0x00) {
            g_status = 0;
        }
    } else {
        while (Wire.available()) {
            Wire.read();
        }
    }
}

static void i2c_on_request()
{
    uint8_t zero = 0;
    switch (g_reg_sel) {
    case REG_STATUS:
        Wire.slaveWrite((const uint8_t *)&g_status, 1);
        break;
    case REG_LEN:
        Wire.slaveWrite((const uint8_t *)&g_len, 1);
        break;
    case REG_TYPE:
        Wire.slaveWrite((const uint8_t *)&g_type, 1);
        break;
    case REG_DATA:
        Wire.slaveWrite((const uint8_t *)g_data, g_len ? g_len : 1);
        break;
    default:
        Wire.slaveWrite(&zero, 1);
        break;
    }
}

static bool cam_init()
{
    camera_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.ledc_timer = LEDC_TIMER_0;
    cfg.pin_d0 = Y2_GPIO_NUM;
    cfg.pin_d1 = Y3_GPIO_NUM;
    cfg.pin_d2 = Y4_GPIO_NUM;
    cfg.pin_d3 = Y5_GPIO_NUM;
    cfg.pin_d4 = Y6_GPIO_NUM;
    cfg.pin_d5 = Y7_GPIO_NUM;
    cfg.pin_d6 = Y8_GPIO_NUM;
    cfg.pin_d7 = Y9_GPIO_NUM;
    cfg.pin_xclk = XCLK_GPIO_NUM;
    cfg.pin_pclk = PCLK_GPIO_NUM;
    cfg.pin_vsync = VSYNC_GPIO_NUM;
    cfg.pin_href = HREF_GPIO_NUM;
    cfg.pin_sccb_sda = SIOD_GPIO_NUM;
    cfg.pin_sccb_scl = SIOC_GPIO_NUM;
    cfg.pin_pwdn = PWDN_GPIO_NUM;
    cfg.pin_reset = RESET_GPIO_NUM;
    cfg.xclk_freq_hz = XCLK_FREQ_HZ;
    cfg.frame_size = FRAMESIZE_VGA;
    cfg.pixel_format = PIXFORMAT_RGB565; /* GC2145 does not support GRAYSCALE */
    cfg.grab_mode = CAMERA_GRAB_LATEST;
    cfg.fb_location = CAMERA_FB_IN_PSRAM;
    cfg.fb_count = 2;
    cfg.jpeg_quality = 12;

    if (esp_camera_init(&cfg) != ESP_OK) {
        return false;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_contrast(s, 2); /* 条码识别需要高对比度 */
    }
    return true;
}

static bool g_cam_ok = false;
static bool g_wire_ok = false;

static char s_stable[13];
static char s_last_reported[13];
static bool s_has_stable = false;
static bool s_has_reported = false;
static int s_stable_cnt = 0;
static uint32_t s_last_ok_ms = 0;

static void publish_isbn(const char code[13])
{
    portENTER_CRITICAL(&g_mux);
    memcpy((void *)g_data, code, 13);
    g_len = 13;
    g_type = 0x01;
    g_status = 1;
    portEXIT_CRITICAL(&g_mux);
    if (Serial) {
        Serial.printf("ISBN: %.13s\n", code);
    }
}

/* zxing-cpp 解码：输入灰度图，输出 13 位 EAN-13 数字。 */
static bool zxing_decode(const uint8_t *gray, int w, int h, char out[13])
{
    using namespace ZXing;
    auto results = ReadBarcodes(
        ImageView(gray, w, h, ImageFormat::Lum),
        ReaderOptions().setFormats(BarcodeFormat::EAN13).setTryHarder(false));
    for (const auto &r : results) {
        if (r.isValid() && r.format() == BarcodeFormat::EAN13) {
            std::string t = r.text();
            if (t.size() >= 13) {
                memcpy(out, t.c_str(), 13);
                return true;
            }
        }
    }
    return false;
}

static void handle_index()
{
    server.send(200, "text/html",
        "<html><body style='margin:0;background:#000'>"
        "<img id=\"f\" src=\"/snapshot\" style=\"width:100%;max-width:800px\">"
        "<script>setInterval(function(){var i=document.getElementById('f');"
        "i.src='/snapshot?t='+Date.now();},800);</script>"
        "</body></html>");
}

static void handle_snapshot()
{
    /* 用识别已算好的灰度图 g_gray 编码 JPEG，不抢摄像头帧缓冲，
     * 避免与识别主循环争抢导致卡顿/识别失败。 */
    uint8_t *jpg = NULL;
    size_t jpg_len = 0;
    if (!g_gray) {
        server.send(503, "text/plain", "no frame");
        return;
    }
    bool ok = fmt2jpg(g_gray, (size_t)CAM_W * CAM_H, CAM_W, CAM_H,
                      PIXFORMAT_GRAYSCALE, 60, &jpg, &jpg_len);
    if (!ok || !jpg || jpg_len == 0) {
        server.send(503, "text/plain", "encode failed");
        return;
    }
    String img((const char *)jpg, jpg_len);
    free(jpg);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "image/jpeg", img);
}

static bool init_wire_slave(void)
{
    if (Wire.begin(I2C_SLAVE_ADDR, I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ)) {
        Wire.onReceive(i2c_on_receive);
        Wire.onRequest(i2c_on_request);
        return true;
    }
    return false;
}

void setup()
{
    Serial.begin(115200);
    g_gray = (uint8_t *)ps_malloc(CAM_W * CAM_H);
    if (!g_gray) {
        g_gray = (uint8_t *)malloc(CAM_W * CAM_H);
    }
    g_wire_ok = init_wire_slave();
    g_cam_ok = cam_init();

    /* WiFi 图传：AP 热点，手机连上后浏览器看实时画面，用于人工对齐条码 */
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    server.on("/", handle_index);
    server.on("/snapshot", handle_snapshot);
    server.on("/favicon.ico", []() { server.send(204); });
    server.begin();
}

void loop()
{
    camera_fb_t *fb;
    char code[13];
    bool ok;

    /* I2C 总线可能被 STM32 主机按住拉低（上电/复位窗口）；每秒重试一次从机
     * 初始化，总线一空闲就能自动成功，不再狂刷日志。 */
    if (!g_wire_ok) {
        static uint32_t s_wire_retry_ms = 0;
        if (millis() - s_wire_retry_ms >= 1000U) {
            s_wire_retry_ms = millis();
            g_wire_ok = init_wire_slave();
        }
    }

    if (!g_cam_ok) {
        g_cam_ok = cam_init();
        if (!g_cam_ok) {
            delay(500);
            return;
        }
    }

    fb = esp_camera_fb_get();
    if (!fb) {
        delay(10);
        return;
    }
    if (g_gray && fb->width == CAM_W && fb->height == CAM_H) {
        /* RGB565 -> grayscale. esp32-camera 的 RGB565 是大端字节序：
         * 高字节 = R[4:0] G[5:3]，低字节 = G[2:0] B[4:0]；
         * 按 little-endian 读 uint16 后，绿色 = px 位[2..0] + 位[15..13]。 */
        const uint16_t *src = (const uint16_t *)fb->buf;
        for (int i = 0; i < CAM_W * CAM_H; ++i) {
            uint16_t px = src[i];
            uint8_t g = (uint8_t)(((px & 0x07U) << 3) | ((px >> 13) & 0x07U));
            g_gray[i] = (uint8_t)(g << 2);
        }
        ok = zxing_decode(g_gray, CAM_W, CAM_H, code);
    } else {
        ok = false;
    }
    esp_camera_fb_return(fb);

    if (ok) {
        s_last_ok_ms = millis();
        if (s_has_stable && memcmp(s_stable, code, 13) == 0) {
            ++s_stable_cnt;
        } else {
            memcpy(s_stable, code, 13);
            s_has_stable = true;
            s_stable_cnt = 1;
        }
        if (s_stable_cnt >= STABLE_FRAMES) {
            bool already = s_has_reported && memcmp(s_last_reported, code, 13) == 0;
            if (!already) {
                memcpy(s_last_reported, code, 13);
                s_has_reported = true;
                publish_isbn(code);
            }
        }
    } else {
        if (millis() - s_last_ok_ms > REARM_MS) {
            s_has_stable = false;
            s_stable_cnt = 0;
            s_has_reported = false; /* 条码移开后允许再次上报同一结果 */
        }
    }

    server.handleClient(); /* 非阻塞：处理浏览器抓图请求，不影响识别 */
    delay(1);
}
