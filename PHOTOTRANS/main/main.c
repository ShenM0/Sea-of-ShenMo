#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "esp_jpeg_dec.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"

#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"

static const char *TAG = "k230_mjpeg";

// --- K230 热点配置（ESP32 作为 STA 连接）---
#define WIFI_SSID          "K230_Video_Link"
#define WIFI_PASS          "12345678"

// --- K230 MJPEG 服务器配置 ---
#define K230_SERVER_IP     "192.168.4.1"    // K230 AP 模式默认 IP（以串口实际输出为准）
#define K230_SERVER_PORT   80
#define MJPEG_STREAM_PATH  "/stream"
#define MJPEG_BOUNDARY     "--FRAME_BOUNDARY"
#define MAX_JPEG_SIZE      (200 * 1024)     // 单帧 JPEG 最大 200KB（640x480@q65）

// --- RGB LCD 配置（实际硬件引脚）---
// 控制信号
#define LCD_PCLK_GPIO       7
#define LCD_VSYNC_GPIO      3
#define LCD_HSYNC_GPIO      46
#define LCD_DE_GPIO         5
#define LCD_DISP_GPIO       -1    // -1 表示不使用
// LCD 时序参数（屏幕物理分辨率 800x480）
#define LCD_H_RES           800
#define LCD_V_RES           480
#define LCD_PCLK_HZ         (16 * 1000 * 1000)
#define LCD_HBP             40
#define LCD_HFP             40
#define LCD_HSYNC_PW        48
#define LCD_VBP             32
#define LCD_VFP             13
#define LCD_VSYNC_PW        3
// RGB565 数据线 (16-bit)
#define LCD_DATA0_GPIO      14    // R3
#define LCD_DATA1_GPIO      38    // R4
#define LCD_DATA2_GPIO      18    // R5
#define LCD_DATA3_GPIO      17    // R6
#define LCD_DATA4_GPIO      10    // R7
#define LCD_DATA5_GPIO      39    // G2
#define LCD_DATA6_GPIO      0     // G3
#define LCD_DATA7_GPIO      45    // G4
#define LCD_DATA8_GPIO      48    // G5
#define LCD_DATA9_GPIO      47    // G6
#define LCD_DATA10_GPIO     21    // G7
#define LCD_DATA11_GPIO     1     // B3
#define LCD_DATA12_GPIO     2     // B4
#define LCD_DATA13_GPIO     42    // B5
#define LCD_DATA14_GPIO     41    // B6
#define LCD_DATA15_GPIO     40    // B7

// 事件组 bit
#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t g_wifi_event_group = NULL;
static int s_retry_num = 0;

// LCD 句柄与双帧缓冲（由驱动分配，位于 PSRAM）
static esp_lcd_panel_handle_t g_lcd_panel = NULL;
static uint8_t *g_fbs[2] = {NULL, NULL};     // 两个全屏帧缓冲
static volatile uint8_t g_cur_fb = 0;        // 当前正在扫描输出的帧缓冲索引
static SemaphoreHandle_t g_vsync_sem = NULL; // VSYNC 同步信号量

// JPEG 接收缓冲与解码输出缓冲（PSRAM）
static uint8_t *g_jpeg_buf = NULL;           // 存放一帧 JPEG
static uint8_t *g_rgb_buf = NULL;            // 解码出的 RGB565（16 字节对齐）

// VSYNC 中断回调：仅释放信号量（必须在 IRAM 中）
static IRAM_ATTR bool lcd_on_vsync(esp_lcd_panel_handle_t panel,
                                   const esp_lcd_rgb_panel_event_data_t *edata,
                                   void *user_ctx)
{
    BaseType_t need_yield = pdFALSE;
    xSemaphoreGiveFromISR(g_vsync_sem, &need_yield);
    return need_yield == pdTRUE;
}

// ==================== LCD 初始化 ====================
static void lcd_init(void)
{
    esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src = LCD_CLK_SRC_PLL240M,
        .data_width = 16,  // RGB565
        .num_fbs = 2,      // 双缓冲
        .bounce_buffer_size_px = LCD_H_RES * 10,  // 加大 bounce buffer，减少 DMA 中断与欠载导致的错位花屏
        .hsync_gpio_num = LCD_HSYNC_GPIO,
        .vsync_gpio_num = LCD_VSYNC_GPIO,
        .de_gpio_num = LCD_DE_GPIO,
        .pclk_gpio_num = LCD_PCLK_GPIO,
        .disp_gpio_num = LCD_DISP_GPIO,
        .data_gpio_nums = {
            LCD_DATA0_GPIO,  LCD_DATA1_GPIO,  LCD_DATA2_GPIO,  LCD_DATA3_GPIO,
            LCD_DATA4_GPIO,  LCD_DATA5_GPIO,  LCD_DATA6_GPIO,  LCD_DATA7_GPIO,
            LCD_DATA8_GPIO,  LCD_DATA9_GPIO,  LCD_DATA10_GPIO, LCD_DATA11_GPIO,
            LCD_DATA12_GPIO, LCD_DATA13_GPIO, LCD_DATA14_GPIO, LCD_DATA15_GPIO,
        },
        .timings = {
            .pclk_hz = LCD_PCLK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_back_porch = LCD_HBP,
            .hsync_front_porch = LCD_HFP,
            .hsync_pulse_width = LCD_HSYNC_PW,
            .vsync_back_porch = LCD_VBP,
            .vsync_front_porch = LCD_VFP,
            .vsync_pulse_width = LCD_VSYNC_PW,
            .flags = {
                .hsync_idle_low = 0,
                .vsync_idle_low = 0,
                .de_idle_high = 0,
                .pclk_active_neg = 0,
                .pclk_idle_high = 0,
            },
        },
        .flags = {
            .fb_in_psram = 1,        // 帧缓冲在 PSRAM 中
            .double_fb = 1,          // 双缓冲
            .bb_invalidate_cache = 1,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_cfg, &g_lcd_panel));
    ESP_LOGI(TAG, "RGB LCD 面板创建成功");

    // 注册 VSYNC 回调，用于无撕裂换页
    g_vsync_sem = xSemaphoreCreateBinary();
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = lcd_on_vsync,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(g_lcd_panel, &cbs, NULL));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(g_lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_lcd_panel));

    // 取出两个帧缓冲并全部清黑（PSRAM 上电内容是随机的，不清屏就是花屏）
    void *fb0 = NULL, *fb1 = NULL;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(g_lcd_panel, 2, &fb0, &fb1));
    g_fbs[0] = (uint8_t *)fb0;
    g_fbs[1] = (uint8_t *)fb1;
    memset(g_fbs[0], 0, LCD_H_RES * LCD_V_RES * 2);
    memset(g_fbs[1], 0, LCD_H_RES * LCD_V_RES * 2);

    ESP_LOGI(TAG, "LCD 初始化完成, %dx%d @ %dMHz",
             LCD_H_RES, LCD_V_RES, (int)(LCD_PCLK_HZ / 1000000));
}

// ==================== 换页显示 ====================
/**
 * @brief 将刚写满的后缓冲切换为扫描输出缓冲（在 VSYNC 边界换页，无撕裂）
 *
 * 把帧缓冲自己的指针传给 draw_bitmap，驱动只做页切换（设置 cur_fb_index），
 * 不做内存拷贝。
 */
static void framebuffer_flip(uint8_t *ready_fb)
{
    // 等待 VSYNC，保证换页发生在垂直消隐期
    xSemaphoreTake(g_vsync_sem, portMAX_DELAY);
    esp_lcd_panel_draw_bitmap(g_lcd_panel, 0, 0, LCD_H_RES, LCD_V_RES, ready_fb);
    g_cur_fb = 1 - g_cur_fb;
}

// ==================== JPEG 解码 + 全屏放大 ====================
static uint16_t s_exp_row[LCD_H_RES];   // 横向放大后的整屏行（DRAM）

/**
 * @brief 将解码出的 RGB565 图像最近邻放大到全屏，写入后缓冲
 */
static void scale_to_fb(const uint16_t *src, uint16_t img_w, uint16_t img_h, uint8_t *back_fb)
{
    int out_y = 0;
    for (int src_y = 0; src_y < img_h; src_y++) {
        const uint16_t *srow = src + (uint32_t)src_y * img_w;
        // 横向最近邻放大到整屏宽度
        for (int x = 0; x < LCD_H_RES; x++) {
            s_exp_row[x] = srow[(x * img_w) / LCD_H_RES];
        }
        // 纵向：把该行写入所有映射到它的输出行
        while (out_y < LCD_V_RES && (out_y * img_h) / LCD_V_RES == src_y) {
            memcpy(back_fb + out_y * LCD_H_RES * 2, s_exp_row, LCD_H_RES * 2);
            out_y++;
        }
    }
}

/**
 * @brief 解码一帧 JPEG 到 g_rgb_buf（RGB565 LE），输出图像宽高
 */
static bool jpeg_decode_to_rgb565(const uint8_t *jpeg_data, int jpeg_len,
                                  uint16_t *out_w, uint16_t *out_h)
{
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;

    jpeg_dec_handle_t dec = NULL;
    if (jpeg_dec_open(&config, &dec) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_open 失败");
        return false;
    }

    jpeg_dec_io_t io = {
        .inbuf = (uint8_t *)jpeg_data,
        .inbuf_len = jpeg_len,
        .outbuf = g_rgb_buf,
    };
    jpeg_dec_header_info_t info = {0};
    bool ok = false;

    if (jpeg_dec_parse_header(dec, &io, &info) != JPEG_ERR_OK) {
        ESP_LOGW(TAG, "JPEG 头解析失败");
    } else if (info.width == 0 || info.height == 0 ||
               info.width > LCD_H_RES || info.height > LCD_V_RES) {
        ESP_LOGW(TAG, "JPEG 尺寸超限: %ux%u", info.width, info.height);
    } else if (jpeg_dec_process(dec, &io) != JPEG_ERR_OK) {
        ESP_LOGW(TAG, "JPEG 解码失败");
    } else {
        *out_w = info.width;
        *out_h = info.height;
        ok = true;
    }

    jpeg_dec_close(dec);
    return ok;
}

// ==================== WiFi STA ====================
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
        // 无限重连：K230 热点可能比 ESP32 晚启动，不能放弃
        s_retry_num++;
        ESP_LOGI(TAG, "WiFi 断开，正在重连 K230 热点... (第 %d 次)", s_retry_num);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "已获取 IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief 初始化 WiFi 为 STA 模式，连接 K230 热点
 */
static void wifi_init_sta(void)
{
    g_wifi_event_group = xEventGroupCreate();

    // 初始化 NVS（Wi-Fi 需要）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化底层 TCP/IP 协议栈
    ESP_ERROR_CHECK(esp_netif_init());
    // 创建默认的事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // 创建默认的 Wi-Fi STA 网络接口
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    // Wi-Fi 初始化
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册 Wi-Fi 与 IP 事件
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &wifi_event_handler,
                                        NULL,
                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                        IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler,
                                        NULL,
                                        NULL));

    // 配置 Wi-Fi STA
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA 已启动，正在连接 K230 热点. SSID:%s", WIFI_SSID);
}

// ==================== MJPEG 流解析 ====================
/**
 * @brief 从 socket 精确读取 N 字节（处理 TCP 分包）
 */
static int recv_all(int sock, uint8_t *buf, int len)
{
    int received = 0;
    while (received < len) {
        int ret = recv(sock, buf + received, len - received, 0);
        if (ret <= 0) {
            return -1;  // 连接断开或错误
        }
        received += ret;
    }
    return received;
}

/**
 * @brief 从 socket 读取一行（以 \n 结尾，去掉行尾 \r\n）
 * @return 行长度（不含 \r\n），<0 表示连接断开或错误
 */
static int recv_line(int sock, char *buf, int maxlen)
{
    int n = 0;
    while (n < maxlen - 1) {
        char c;
        int ret = recv(sock, &c, 1, 0);
        if (ret <= 0) {
            return -1;
        }
        if (c == '\n') {
            break;
        }
        if (c != '\r') {
            buf[n++] = c;
        }
    }
    buf[n] = '\0';
    return n;
}

/**
 * @brief 读取 HTTP/MJPEG 头部块（直到空行为止），检查是否为 200 OK
 * @return true 表示 200 OK
 */
static bool http_read_response_headers(int sock)
{
    char line[128];
    bool is_ok = false;
    bool first_line = true;

    while (1) {
        int n = recv_line(sock, line, sizeof(line));
        if (n < 0) {
            return false;
        }
        if (n == 0) {
            break;  // 空行，头部结束
        }
        if (first_line) {
            ESP_LOGI(TAG, "HTTP 响应: %s", line);
            is_ok = (strstr(line, "200") != NULL);
            first_line = false;
        }
    }
    return is_ok;
}

/**
 * @brief 从 MJPEG 流中读取一帧 JPEG
 *
 * 流格式：--FRAME_BOUNDARY\r\n
 *         Content-Type: image/jpeg\r\n
 *         Content-Length: N\r\n
 *         \r\n
 *         <JPEG data>\r\n
 *
 * @return JPEG 长度，<0 表示连接断开或协议错误
 */
static int mjpeg_read_frame(int sock, uint8_t *jpeg_buf)
{
    char line[128];
    int content_len = -1;

    // 逐行读帧头，直到空行；同步到 boundary 行
    bool got_boundary = false;
    while (1) {
        int n = recv_line(sock, line, sizeof(line));
        if (n < 0) {
            return -1;
        }
        if (!got_boundary) {
            // 等待 boundary 行（容忍流中混入的杂散数据）
            if (strncmp(line, MJPEG_BOUNDARY, strlen(MJPEG_BOUNDARY)) == 0) {
                got_boundary = true;
            }
            continue;
        }
        if (n == 0) {
            break;  // 空行，帧头结束
        }
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            content_len = atoi(line + 15);
        }
    }

    if (!got_boundary || content_len <= 0 || content_len > MAX_JPEG_SIZE) {
        ESP_LOGW(TAG, "MJPEG 帧头异常: boundary=%d len=%d", got_boundary, content_len);
        return -1;
    }

    // 读取 JPEG 数据
    if (recv_all(sock, jpeg_buf, content_len) < 0) {
        return -1;
    }

    // 读取帧尾 \r\n（读不出来也不算致命错误）
    uint8_t tail[2];
    recv_all(sock, tail, 2);

    return content_len;
}

// ==================== MJPEG 客户端任务 ====================
/**
 * @brief MJPEG 客户端任务：连接 K230 HTTP 服务器，拉取 MJPEG 流，解码并显示
 */
static void mjpeg_client_task(void *pvParameters)
{
    // 从 PSRAM 分配 JPEG 接收缓冲和解码输出缓冲（16 字节对齐，解码器要求）
    g_jpeg_buf = heap_caps_malloc(MAX_JPEG_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_rgb_buf = heap_caps_aligned_alloc(16, LCD_H_RES * LCD_V_RES * 2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (g_jpeg_buf == NULL || g_rgb_buf == NULL) {
        ESP_LOGE(TAG, "PSRAM 缓冲分配失败!");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        // 等待 WiFi 连接成功
        xEventGroupWaitBits(g_wifi_event_group, WIFI_CONNECTED_BIT,
                            pdFALSE, pdTRUE, portMAX_DELAY);

        // 创建 socket 并连接 K230
        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) {
            ESP_LOGE(TAG, "创建 socket 失败: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        struct sockaddr_in server_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(K230_SERVER_PORT),
        };
        inet_pton(AF_INET, K230_SERVER_IP, &server_addr.sin_addr);

        ESP_LOGI(TAG, "正在连接 K230 服务器 %s:%d ...", K230_SERVER_IP, K230_SERVER_PORT);
        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            ESP_LOGW(TAG, "连接 K230 失败: errno %d，1 秒后重试", errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // 关闭 Nagle，提高响应速度
        int nodelay = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        // 发送 HTTP GET 请求 MJPEG 流
        char request[128];
        int req_len = snprintf(request, sizeof(request),
                               "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n",
                               MJPEG_STREAM_PATH, K230_SERVER_IP);
        if (send(sock, request, req_len, 0) < 0) {
            ESP_LOGW(TAG, "发送 HTTP 请求失败: errno %d", errno);
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // 读取并校验 HTTP 响应头
        if (!http_read_response_headers(sock)) {
            ESP_LOGW(TAG, "HTTP 响应异常，断开重连");
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "MJPEG 流已建立，开始接收图像...");

        // 持续接收 MJPEG 帧
        int frame_count = 0;
        int64_t stat_start = esp_timer_get_time();

        while (1) {
            int jpeg_len = mjpeg_read_frame(sock, g_jpeg_buf);
            if (jpeg_len < 0) {
                ESP_LOGW(TAG, "MJPEG 流断开，重新连接...");
                break;
            }

            // 解码并放大到全屏，写入"后缓冲"（不写正在显示的缓冲，无撕裂）
            uint16_t img_w = 0, img_h = 0;
            if (jpeg_decode_to_rgb565(g_jpeg_buf, jpeg_len, &img_w, &img_h)) {
                uint8_t *back_fb = g_fbs[1 - g_cur_fb];
                scale_to_fb((const uint16_t *)g_rgb_buf, img_w, img_h, back_fb);

                // VSYNC 边界换页，把新帧切到屏幕上
                framebuffer_flip(back_fb);

                // 帧率统计
                frame_count++;
                if (frame_count % 30 == 0) {
                    float fps = 30.0f / ((esp_timer_get_time() - stat_start) / 1000000.0f);
                    ESP_LOGI(TAG, "FPS: %.1f, 帧大小: %dB", fps, jpeg_len);
                    stat_start = esp_timer_get_time();
                }
            }
        }

        close(sock);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // 不会执行到这里
    vTaskDelete(NULL);
}

void app_main(void)
{
    // 1. 初始化 LCD
    lcd_init();

    // 2. 初始化 WiFi STA，连接 K230 热点
    wifi_init_sta();

    // 3. 等待 WiFi 首次连接（超时也继续，由客户端任务持续等待/重试）
    EventBits_t bits = xEventGroupWaitBits(g_wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE, pdTRUE, pdMS_TO_TICKS(30000));
    if ((bits & WIFI_CONNECTED_BIT) == 0) {
        ESP_LOGW(TAG, "30 秒内未连上 K230 热点，将在后台继续重试");
    }

    // 4. 启动 MJPEG 客户端任务
    xTaskCreate(mjpeg_client_task, "mjpeg_client", 8192, NULL, 5, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
