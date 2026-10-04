/* Software (bit-banged) I2C master on PB6/PB7, ported from the dc39f PN532
 * reference project (MindX zhujun). Bit-banging avoids the STM32F1 hardware
 * I2C quirks with the PN532's clock stretching.
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

static void sw_i2c_send_byte(uint8_t byte)
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
        while (SCL_READ() == 0U) { /* wait for slave clock-stretch release */
            SCL_H();
        }
        SCL_L();
        sw_delay_us(5);
        byte <<= 1;
    }
    SCL_L();
    sw_delay_us(5);
}

static uint8_t sw_i2c_recv_byte(void)
{
    uint8_t i;
    uint8_t byte = 0U;
    for (i = 0U; i < 8U; ++i) {
        SCL_H();
        sw_delay_us(5);
        byte <<= 1;
        if (SDA_READ()) {
            byte |= 1U;
        }
        SCL_L();
        sw_delay_us(5);
    }
    return byte;
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
    sw_i2c_send_byte(addr8);
    if (sw_i2c_wait_ack()) {
        sw_i2c_stop();
        return 0U;
    }
    while (len--) {
        sw_i2c_send_byte(*buf++);
        sw_i2c_wait_ack();
    }
    sw_i2c_stop();
    return 1U;
}

uint8_t sw_i2c_read(uint8_t *buf, uint16_t len, uint8_t addr8)
{
    sw_i2c_start();
    sw_i2c_send_byte(addr8);
    if (sw_i2c_wait_ack()) {
        sw_i2c_stop();
        return 0U;
    }
    while (len) {
        if (len == 1U) {
            *buf++ = sw_i2c_recv_byte();
            sw_i2c_nack();
        } else {
            *buf++ = sw_i2c_recv_byte();
            sw_i2c_ack();
        }
        len--;
    }
    sw_i2c_stop();
    return 1U;
}
