/* PN532 NFC module driver, using a software (bit-banged) I2C master on
 * PB6/PB7, ported from the dc39f PN532 reference project (MindX zhujun).
 *
 * Wire format per read transaction (a 0x01 status byte is always prepended):
 *   [0x01][00 00 FF LEN LCS D5 CMD DATA... DCS 00]
 * Immediately after a command write the PN532 first returns an ACK frame
 *   [0x01][00 00 FF 00 FF 00]   (LEN == 0)
 * which must be skipped while polling for the real response (LEN >= 3, TFI=D5).
 * The whole response (status + frame) is read in ONE transaction.
 *
 * We have no IRQ or RST pin wired (4-wire hookup), so readiness is detected
 * by polling the status byte instead of the IRQ pin.
 */

#include "pn532.h"
#include "main.h"
#include "packet.h"
#include "sw_i2c.h"

#include <string.h>

#define PN532_WRITE_ADDR ((uint8_t)(PN532_I2C_ADDR << 1))        /* 0x48 */
#define PN532_READ_ADDR  ((uint8_t)((PN532_I2C_ADDR << 1) | 1U)) /* 0x49 */

#define PN532_PREAMBLE     0x00U
#define PN532_STARTCODE1   0x00U
#define PN532_STARTCODE2   0xFFU
#define PN532_TFI_HOST     0xD4U
#define PN532_TFI_PN532    0xD5U

#define PN532_CMD_SAMCONFIGURATION     0x14U
#define PN532_CMD_INLISTPASSIVETARGET  0x4AU

#define PN532_POLL_INTERVAL_MS 200U

extern struct PacketController packet_controller;

static int pn532_write_command(uint8_t cmd, const uint8_t *data, uint8_t data_len)
{
    uint8_t frame[64];
    uint8_t len = (uint8_t)(data_len + 2U);
    uint8_t sum = (uint8_t)(PN532_TFI_HOST + cmd);
    uint8_t i;

    frame[0] = PN532_PREAMBLE;
    frame[1] = PN532_STARTCODE1;
    frame[2] = PN532_STARTCODE2;
    frame[3] = len;
    frame[4] = (uint8_t)(0x100 - len);           /* LCS */
    frame[5] = PN532_TFI_HOST;                   /* TFI */
    frame[6] = cmd;
    for (i = 0U; i < data_len; ++i) {
        frame[7U + i] = data[i];
        sum = (uint8_t)(sum + data[i]);
    }
    frame[7U + data_len] = (uint8_t)(0x100 - sum); /* DCS */
    frame[8U + data_len] = 0x00U;                  /* postamble */

    return sw_i2c_write(frame, (uint16_t)(9U + data_len), PN532_WRITE_ADDR) ? 0 : -1;
}

/* Poll for the real response (skipping the ACK whose LEN == 0). On success the
 * full stream [status, frame...] is copied into buf and its length returned. */
static int pn532_read_response(uint8_t *buf, uint16_t buf_size, uint16_t timeout_ms)
{
    uint8_t rbuf[32];
    uint32_t deadline = HAL_GetTick() + timeout_ms;

    for (;;) {
        if (sw_i2c_read(rbuf, sizeof(rbuf), PN532_READ_ADDR)) {
            if (rbuf[0] == 0x01U &&
                rbuf[1] == PN532_PREAMBLE &&
                rbuf[2] == PN532_STARTCODE1 &&
                rbuf[3] == PN532_STARTCODE2) {
                uint8_t len = rbuf[4];
                if (len >= 3U &&
                    rbuf[5] == (uint8_t)(0x100 - len) &&
                    rbuf[6] == PN532_TFI_PN532) {
                    uint16_t total = (uint16_t)(7U + len);
                    if (total <= buf_size) {
                        memcpy(buf, rbuf, total);
                        return (int)total;
                    }
                }
            }
        }
        if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
            return -1; /* timeout */
        }
    }
}

int pn532_init(void)
{
    uint8_t sam[2] = {0x01U, 0x00U}; /* normal mode, timeout 0 (matches reference) */
    uint8_t resp[32];
    int r;

    sw_i2c_init();

    r = pn532_write_command(PN532_CMD_SAMCONFIGURATION, sam, 2U);
    if (r != 0) {
        return r;
    }
    r = pn532_read_response(resp, sizeof(resp), 200U);
    if (r < 0) {
        return r;
    }
    return (resp[7] == (uint8_t)(PN532_CMD_SAMCONFIGURATION + 1U)) ? 0 : -10;
}

int pn532_read_passive_target_id(uint8_t *uid, uint8_t *uid_len)
{
    uint8_t cmd_data[2] = {0x01U, 0x00U}; /* MaxTg=1, BrTy=0x00 (106k type A) */
    uint8_t resp[32];
    uint8_t nfcid_len;
    int r;

    r = pn532_write_command(PN532_CMD_INLISTPASSIVETARGET, cmd_data, 2U);
    if (r != 0) {
        return -1;
    }
    r = pn532_read_response(resp, sizeof(resp), 500U);
    if (r < 0) {
        return -2;
    }
    /* resp: [0]=status [1..3]=preamble [4]=LEN [5]=LCS [6]=TFI(D5)
     *       [7]=CMD(4B) [8]=NbTg ... [13]=NFCID_LEN [14..]=UID */
    if (resp[7] != (uint8_t)(PN532_CMD_INLISTPASSIVETARGET + 1U)) {
        return -3;
    }
    if (resp[8] == 0U) {
        return 1; /* NbTg = 0: no card present */
    }

    nfcid_len = resp[13];
    if (nfcid_len == 0U || nfcid_len > PN532_UID_MAX_LEN) {
        return -6;
    }
    memcpy(uid, &resp[14], nfcid_len);
    *uid_len = nfcid_len;
    return 0;
}

int pn532_i2c_scan(uint8_t *addrs, uint8_t max_n)
{
    uint8_t dummy = 0U;
    uint8_t found = 0U;
    uint16_t addr;

    sw_i2c_init();
    for (addr = 0x08U; addr <= 0x77U; ++addr) {
        if (sw_i2c_write(&dummy, 0U, (uint8_t)(addr << 1))) {
            if (found < max_n) {
                addrs[found] = (uint8_t)addr;
            }
            ++found;
        }
    }
    return (int)found;
}

int pn532_raw_dump(uint8_t *buf, uint16_t len)
{
    if (buf == 0 || len == 0U) {
        return 0;
    }
    sw_i2c_init();
    sw_i2c_read(buf, len, PN532_READ_ADDR);
    return (int)len;
}

int pn532_dump_response(uint8_t *buf, uint16_t len)
{
    uint8_t cmd_data[2] = {0x01U, 0x00U}; /* MaxTg=1, BrTy=0x00 */
    int r;

    if (buf == 0 || len == 0U) {
        return -1;
    }
    r = pn532_write_command(PN532_CMD_INLISTPASSIVETARGET, cmd_data, 2U);
    if (r != 0) {
        return -1;
    }
    HAL_Delay(150); /* let the PN532 scan the RF field and build the reply */
    sw_i2c_read(buf, len, PN532_READ_ADDR);
    return (int)len;
}

void nfc_task_poll(void)
{
    static uint8_t last_uid[PN532_UID_MAX_LEN];
    static uint8_t last_uid_len = 0U;
    static uint8_t err_count = 0U;
    static uint32_t next_poll_ms = 0U;

    uint32_t now = HAL_GetTick();
    uint8_t uid[PN532_UID_MAX_LEN];
    uint8_t uid_len = 0U;
    int r;

    if ((int32_t)(now - next_poll_ms) < 0) {
        return;
    }
    next_poll_ms = now + PN532_POLL_INTERVAL_MS;

    r = pn532_read_passive_target_id(uid, &uid_len);
    if (r == 0) {
        err_count = 0U;
        if (uid_len != last_uid_len || memcmp(uid, last_uid, uid_len) != 0) {
            uint8_t report[2U + PN532_UID_MAX_LEN];
            memcpy(last_uid, uid, uid_len);
            last_uid_len = uid_len;
            report[0] = 0x01U; /* sub-cmd: card detected */
            report[1] = uid_len;
            memcpy(&report[2], uid, uid_len);
            packet_controller.transmit(&packet_controller, PACKET_FUNC_NFC,
                                       report, 2U + uid_len);
        }
    } else if (r == 1) {
        err_count = 0U;
        last_uid_len = 0U; /* card removed: allow the same UID to fire again */
    } else {
        /* I2C/protocol error (e.g. PN532 missing or wrong mode): back off to
         * keep the main loop responsive, then retry. */
        ++err_count;
        if (err_count > 5U) {
            next_poll_ms = now + 2000U;
            err_count = 0U;
        }
    }
}
