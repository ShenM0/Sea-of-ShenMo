/* Software (bit-banged) I2C master on PB6/PB7, ported from the dc39f PN532
 * reference project (MindX zhujun). Bit-banging avoids the STM32F1 hardware
 * I2C quirks with the PN532's clock stretching.
 *
 * All clock-stretch waits are bounded (SW_I2C_STRETCH_TIMEOUT_MS): if a slave
 * (e.g. the ESP32-S3-Cam, which reboots/glitches) holds SCL low forever, the
 * transaction times out, the bus is recovered, and an error is returned so the
 * main loop never hangs.
 */

#include "sw_i2c.h"
#include "main.h"

#define SW_SCL_PORT GPIOB
#define SW_SCL_PIN  GPIO_PIN_6
#define SW_SDA_PORT GPIOB
#define SW_SDA_PIN  GPIO_PIN_7

#define SCL_H() HAL_GPIO_WritePin(SW_SCL_PORT, SW_SCL_PIN, GPIO_PIN_SET)
#define SCL_L() HAL_GPIO_WritePin(SW_SCL_PORT, SW_SCL_PIN, GPIO_PIN_RESET)
#define SDA_H() HAL_GPIO_WritePin(SW_SDA_PORT, SW_SDA_PIN, GPIO_PIN_SET)
#define SDA_L() HAL_GPIO_WritePin(SW_SDA_PORT, SW_SDA_PIN, GPIO_PIN_RESET)
#define SCL_READ() HAL_GPIO_ReadPin(SW_SCL_PORT, SW_SCL_PIN)
#define SDA_READ() HAL_GPIO_ReadPin(SW_SDA_PORT, SW_SDA_PIN)

/* Max time to wait for a slave to release SCL (clock stretching). After this
 * the bus is considered stuck and is recovered. */
#define SW_I2C_STRETCH_TIMEOUT_MS 10U

/* Rough microsecond busy delay (calibrated for ~72 MHz core). I2C timing is
 * tolerant, so a factor of two either way is harmless. */
static void sw_delay_us(uint32_t us)
{
    volatile uint32_t i;
    while (us--) {
        i = 12U;
        while (i--) {
            __NOP();
        }
    }
}

static void sw_i2c_start(void)
{
    SDA_H();
    SCL_H();
    sw_delay_us(5);
    SDA_L();
    sw_delay_us(5);
    SCL_L();
    sw_delay_us(10);
}

static void sw_i2c_stop(void)
{
    SCL_L();
    SDA_L();
    sw_delay_us(5);
    SCL_H();
    sw_delay_us(5);
    SDA_H();
    sw_delay_us(5);
}

/* Wait for the slave to release SCL (clock stretching). Returns 1 on release,
 * 0 on timeout (bus stuck). */
static uint8_t sw_i2c_wait_scl_release(void)
{
    uint32_t deadline = HAL_GetTick() + SW_I2C_STRETCH_TIMEOUT_MS;
    while (SCL_READ() == 0U) {
        if ((int32_t)(HAL_GetTick() - deadline) >= 0) {
            return 0U;
        }
        SCL_H();
    }
    return 1U;
}

/* Recover a stuck bus: with SDA released, toggling SCL 9+ times forces a slave
 * holding SDA low to release it, then leave both lines idle high. */
static void sw_i2c_bus_recover(void)
{
    uint8_t i;
    SDA_H();
    for (i = 0U; i < 10U; ++i) {
        SCL_H();
        sw_delay_us(5);
        SCL_L();
        sw_delay_us(5);
    }
    SCL_H();
    sw_delay_us(5);
}

static uint8_t sw_i2c_wait_ack(void)
{
    uint8_t ack;
    SDA_H();
    sw_delay_us(5);
    SCL_H();
    sw_delay_us(5);
    ack = SDA_READ() ? 1U : 0U;
    SCL_L();
    sw_delay_us(5);
    return ack;
}

static uint8_t sw_i2c_send_byte(uint8_t byte)
{
    uint8_t i;
    for (i = 0U; i < 8U; ++i) {
        if (byte & 0x80U) {
            SDA_H();
        } else {
            SDA_L();
        }
        sw_delay_us(5);
        SCL_H();
        sw_delay_us(5);
        if (!sw_i2c_wait_scl_release()) {
            return 0U; /* bus stuck */
        }
        SCL_L();
        sw_delay_us(5);
        byte <<= 1;
    }
    SCL_L();
    sw_delay_us(5);
    return 1U;
}

static uint8_t sw_i2c_recv_byte(uint8_t *out)
{
    uint8_t i;
    uint8_t byte = 0U;
    for (i = 0U; i < 8U; ++i) {
        SCL_H();
        if (!sw_i2c_wait_scl_release()) {
            return 0U; /* bus stuck */
        }
        sw_delay_us(5);
        byte <<= 1;
        if (SDA_READ()) {
            byte |= 1U;
        }
        SCL_L();
        sw_delay_us(5);
    }
    *out = byte;
    return 1U;
}

static void sw_i2c_ack(void)
{
    SCL_L();
    SDA_L();
    sw_delay_us(5);
    SCL_H();
    sw_delay_us(5);
    SCL_L();
    sw_delay_us(5);
    SDA_H();
}

static void sw_i2c_nack(void)
{
    SCL_L();
    SDA_H();
    sw_delay_us(5);
    SCL_H();
    sw_delay_us(5);
    SCL_L();
    sw_delay_us(5);
}

void sw_i2c_init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = SW_SCL_PIN | SW_SDA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
    SCL_H();
    SDA_H();
}

uint8_t sw_i2c_write(const uint8_t *buf, uint16_t len, uint8_t addr8)
{
    sw_i2c_start();
    if (!sw_i2c_send_byte(addr8)) {
        sw_i2c_bus_recover();
        return 0U;
    }
    if (sw_i2c_wait_ack()) {
        sw_i2c_stop();
        return 0U;
    }
    while (len--) {
        if (!sw_i2c_send_byte(*buf++)) {
            sw_i2c_bus_recover();
            return 0U;
        }
        sw_i2c_wait_ack();
    }
    sw_i2c_stop();
    return 1U;
}

uint8_t sw_i2c_read(uint8_t *buf, uint16_t len, uint8_t addr8)
{
    uint8_t b;
    sw_i2c_start();
    if (!sw_i2c_send_byte(addr8)) {
        sw_i2c_bus_recover();
        return 0U;
    }
    if (sw_i2c_wait_ack()) {
        sw_i2c_stop();
        return 0U;
    }
    while (len) {
        if (!sw_i2c_recv_byte(&b)) {
            sw_i2c_bus_recover();
            return 0U;
        }
        *buf++ = b;
        if (len == 1U) {
            sw_i2c_nack();
        } else {
            sw_i2c_ack();
        }
        len--;
    }
    sw_i2c_stop();
    return 1U;
}
