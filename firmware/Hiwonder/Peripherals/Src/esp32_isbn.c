/* Filename: esp32_isbn.c
 * ESP32-S3-Cam ISBN (EAN-13) result reader over the software I2C on PB6/PB7.
 *
 * We read the ESP32 slave with the bit-banged sw_i2c master (not the STM32F1
 * hardware I2C1) because the ESP32 stretches SCL while preparing its reply in
 * the onRequest handler - exactly the clock-stretch case the F1 hardware I2C
 * does not tolerate (same failure mode as the PN532).
 *
 * The PN532 (0x24) and the ESP32 (0x52) share the same PB6/PB7 bus. Both are
 * polled sequentially in the main loop, so there is no concurrent access.
 */

#include "esp32_isbn.h"
#include "main.h"
#include "packet.h"
#include "sw_i2c.h"

#include <string.h>

#define ESP32_ISBN_WRITE_ADDR ((uint8_t)(ESP32_ISBN_I2C_ADDR << 1))        /* 0xA4 */
#define ESP32_ISBN_READ_ADDR  ((uint8_t)((ESP32_ISBN_I2C_ADDR << 1) | 1U)) /* 0xA5 */

#define ESP32_ISBN_POLL_INTERVAL_MS 100U

extern struct PacketController packet_controller;

static uint32_t s_last_poll_ms = 0U;
static uint8_t s_err_count = 0U;

int esp32_isbn_read_reg(uint8_t reg, uint8_t *buf, uint16_t len)
{
    if (buf == 0 || len == 0U) {
        return -1;
    }
    /* Select register, then read it in a separate transaction (STOP in
     * between is fine: the ESP32 onReceive / onRequest are separate calls). */
    if (!sw_i2c_write(&reg, 1U, ESP32_ISBN_WRITE_ADDR)) {
        return -2; /* select register failed (no ACK from slave) */
    }
    if (!sw_i2c_read(buf, len, ESP32_ISBN_READ_ADDR)) {
        return -3; /* read failed */
    }
    return 0;
}

int esp32_isbn_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t w[2];

    w[0] = reg;
    w[1] = val;
    return sw_i2c_write(w, 2U, ESP32_ISBN_WRITE_ADDR) ? 0 : -1;
}

int esp32_isbn_read_result(uint8_t *type, uint8_t *data, uint8_t *len)
{
    uint8_t status = 0U;
    uint8_t l = 0U;
    uint8_t t = 0U;
    int r;

    r = esp32_isbn_read_reg(ESP32_ISBN_REG_STATUS, &status, 1U);
    if (r != 0) {
        return r;
    }
    if (status != 1U) {
        return 1; /* no result ready */
    }

    r = esp32_isbn_read_reg(ESP32_ISBN_REG_LEN, &l, 1U);
    if (r != 0) {
        return r;
    }
    if (l == 0U || l > ESP32_ISBN_MAX_LEN) {
        l = ESP32_ISBN_MAX_LEN;
    }

    r = esp32_isbn_read_reg(ESP32_ISBN_REG_TYPE, &t, 1U);
    if (r != 0) {
        return r;
    }

    r = esp32_isbn_read_reg(ESP32_ISBN_REG_DATA, data, (uint16_t)l);
    if (r != 0) {
        return r;
    }

    /* Acknowledge: clear STATUS so the ESP32 can raise it again later. */
    esp32_isbn_write_reg(ESP32_ISBN_REG_STATUS, 0x00U);

    *type = t;
    *len = l;
    return 0;
}

void esp32_isbn_task_poll(void)
{
    uint8_t data[ESP32_ISBN_MAX_LEN];
    uint8_t type = 0U;
    uint8_t len = 0U;
    uint8_t resp[3U + ESP32_ISBN_MAX_LEN];
    uint32_t now = HAL_GetTick();
    int r;

    if ((int32_t)(now - s_last_poll_ms) < ESP32_ISBN_POLL_INTERVAL_MS) {
        return;
    }
    s_last_poll_ms = now;

    r = esp32_isbn_read_result(&type, data, &len);
    if (r == 0 && len > 0U) {
        s_err_count = 0U;
        resp[0] = 0x01U; /* sub-command: result report */
        resp[1] = type;
        resp[2] = len;
        memcpy(&resp[3], data, len);
        packet_controller.transmit(&packet_controller, PACKET_FUNC_ISBN,
                                   resp, (uint16_t)(3U + len));
    } else if (r < 0) {
        /* slave absent/booting: back off so the bus stays quiet and the ESP32
         * gets a clean window to finish its own I2C slave init */
        ++s_err_count;
        if (s_err_count > 5U) {
            s_last_poll_ms = now + 2000U;
            s_err_count = 0U;
        }
    }
}
