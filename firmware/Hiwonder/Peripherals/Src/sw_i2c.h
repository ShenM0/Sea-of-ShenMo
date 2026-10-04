/* Filename: sw_i2c.h
 * Software (bit-banged) I2C master on PB6 (SCL) / PB7 (SDA), ported from the
 * dc39f PN532 reference project (MindX zhujun). The STM32F1 hardware I2C is
 * unreliable with the PN532's clock stretching (partial-read NACK / bus hang),
 * so the PN532 driver uses this bit-bang master instead.
 *
 * ASCII-only comments to avoid GBK/UTF-8 mixing in the Keil project.
 */

#ifndef __SW_I2C_H_
#define __SW_I2C_H_

#include <stdint.h>

/* Configure PB6/PB7 as open-drain GPIO and idle the bus high. */
void sw_i2c_init(void);

/* Write len bytes to an 8-bit I2C address (addr8 already includes R/W=0).
 * Returns 1 on success (address ACKed), 0 on address NACK. */
uint8_t sw_i2c_write(const uint8_t *buf, uint16_t len, uint8_t addr8);

/* Read len bytes from an 8-bit I2C address (addr8 includes R/W=1).
 * Returns 1 on success, 0 on address NACK. Data bytes are read regardless
 * of the slave's ACK (missing bytes read back as 0xFF). */
uint8_t sw_i2c_read(uint8_t *buf, uint16_t len, uint8_t addr8);

#endif /* __SW_I2C_H_ */
