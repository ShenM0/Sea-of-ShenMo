/* Filename: esp32_isbn.h
 * ESP32-S3-Cam ISBN (EAN-13) result reader over the software I2C on PB6/PB7.
 *
 * The ESP32-S3-Cam runs its own firmware: it decodes EAN-13 barcodes locally
 * and exposes the result as an I2C slave (address 0x52). This driver polls
 * that slave from the STM32 and reports results over the serial packet
 * channel (PACKET_FUNC_ISBN, sub-command 0x01).
 *
 * Register protocol of the ESP32 slave (write one byte = register index, then
 * read the register value; writing [0x00, 0x00] clears STATUS):
 *   0x00 STATUS  0=idle, 1=result ready
 *   0x01 LEN     result byte count
 *   0x02 TYPE    0x01=ISBN
 *   0x03 DATA    result bytes (LEN of them)
 *
 * All comments are ASCII to avoid GBK/UTF-8 mixing inside the Keil project.
 */

#ifndef __ESP32_ISBN_H_
#define __ESP32_ISBN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 7-bit I2C address of the ESP32-S3-Cam slave (matches vendor tutorial 0x52). */
#define ESP32_ISBN_I2C_ADDR 0x52U

#define ESP32_ISBN_MAX_LEN 16U

#define ESP32_ISBN_REG_STATUS 0x00U
#define ESP32_ISBN_REG_LEN    0x01U
#define ESP32_ISBN_REG_TYPE   0x02U
#define ESP32_ISBN_REG_DATA   0x03U

/* Debug: select a register (write its index) then read len bytes.
 * Returns 0 on success, negative on I2C error. */
int esp32_isbn_read_reg(uint8_t reg, uint8_t *buf, uint16_t len);

/* Debug: write [reg, val] (used to clear STATUS). Returns 0 or negative. */
int esp32_isbn_write_reg(uint8_t reg, uint8_t val);

/* One-shot: if a result is ready, read TYPE/DATA and clear STATUS.
 * Fills *type/*data/*len. Returns 0 with a result, 1 when none ready,
 * negative on I2C error. */
int esp32_isbn_read_result(uint8_t *type, uint8_t *data, uint8_t *len);

/* Main-loop polling task: read result when ready, report via PACKET_FUNC_ISBN. */
void esp32_isbn_task_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP32_ISBN_H_ */
