/* Filename: pn532.h
 * PN532 NFC module driver over a software (bit-banged) I2C on PB6/PB7.
 *
 * Provides: I2C bus scan (debug), one-shot passive-target (UID) read, and a
 * polling task that reports a new card UID over the serial packet channel
 * (PACKET_FUNC_NFC, sub-command 0x01).
 *
 * All comments are ASCII to avoid GBK/UTF-8 mixing inside the Keil project.
 */

#ifndef __PN532_H_
#define __PN532_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 7-bit I2C address of the PN532. Measured 0x24 on this module (the I2C
 * scan command, sub-cmd 0x02, found it). Some modules use 0x48 instead;
 * re-run the scan and update this define if yours differs. */
#define PN532_I2C_ADDR 0x24U

#define PN532_UID_MAX_LEN 10U

/* One-shot card search: returns 0 and fills uid/uid_len on success,
 * 1 when no card is present, negative on I2C/protocol error. */
int pn532_read_passive_target_id(uint8_t *uid, uint8_t *uid_len);

/* Scan I2C1 bus (0x08..0x77), return number of found devices, fill addrs. */
int pn532_i2c_scan(uint8_t *addrs, uint8_t max_n);

/* Debug: raw-read up to len bytes (one by one) from the PN532 without sending
 * a command. Returns the number of bytes actually read. */
int pn532_raw_dump(uint8_t *buf, uint16_t len);

/* Debug: send InListPassiveTarget, wait, then raw-read up to len reply bytes.
 * Returns the number of bytes read, or negative if the write failed. */
int pn532_dump_response(uint8_t *buf, uint16_t len);

/* Configure the PN532 (SAM normal mode). Called once at startup. */
int pn532_init(void);

/* Main-loop polling task: detect card, report a new UID via PACKET_FUNC_NFC. */
void nfc_task_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __PN532_H_ */
