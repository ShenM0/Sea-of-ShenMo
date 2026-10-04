#include "i2c_bus_manager.h"
#include "i2c.h"
#include "main.h"

static volatile bool g_i2c1_locked = false;
static uint32_t g_i2c1_last_recover_ms = 0U;

void i2c1_bus_lock(void)
{
    uint32_t primask;

    for (;;) {
        primask = __get_PRIMASK();
        __disable_irq();
        if (!g_i2c1_locked) {
            g_i2c1_locked = true;
            __set_PRIMASK(primask);
            return;
        }
        __set_PRIMASK(primask);
    }
}

void i2c1_bus_unlock(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    g_i2c1_locked = false;
    __set_PRIMASK(primask);
}

/* Release a stuck I2C slave (e.g. a PN532 left holding SDA low after an
 * aborted frame) by toggling SCL, then re-initialise I2C1. */
static void i2c1_bus_recover(void)
{
    GPIO_InitTypeDef gpio;
    uint8_t i;

    HAL_I2C_DeInit(&hi2c1);

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET); /* release SDA */
    for (i = 0U; i < 10U; ++i) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
        HAL_Delay(1);
    }
    /* STOP condition: SDA low -> high while SCL high */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);

    MX_I2C1_Init();
}

/* Recover on bus-stuck errors only (NACK = HAL_ERROR is normal, skip it). */
static void i2c1_on_error(HAL_StatusTypeDef status)
{
    uint32_t now = HAL_GetTick();

    if (status != HAL_TIMEOUT && status != HAL_BUSY) {
        return;
    }
    if ((int32_t)(now - g_i2c1_last_recover_ms) < 500) {
        return;
    }
    g_i2c1_last_recover_ms = now;
    i2c1_bus_recover();
}

int i2c1_write(uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef status;

    if (addr > 0x7FU || data == 0 || len == 0U) {
        return (int)HAL_ERROR;
    }
    i2c1_bus_lock();
    status = HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(addr << 1), reg,
                               I2C_MEMADD_SIZE_8BIT, (uint8_t *)data,
                               len, 100U);
    i2c1_bus_unlock();
    return (int)status;
}

int i2c1_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    HAL_StatusTypeDef status;

    if (addr > 0x7FU || buf == 0 || len == 0U) {
        return (int)HAL_ERROR;
    }
    i2c1_bus_lock();
    status = HAL_I2C_Mem_Read(&hi2c1, (uint16_t)(addr << 1), reg,
                              I2C_MEMADD_SIZE_8BIT, buf, len, 100U);
    i2c1_bus_unlock();
    return (int)status;
}

int i2c1_raw_write(uint8_t addr, const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef status;

    if (addr > 0x7FU || data == 0 || len == 0U) {
        return (int)HAL_ERROR;
    }
    i2c1_bus_lock();
    status = HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(addr << 1),
                                     (uint8_t *)data, len, 100U);
    i2c1_bus_unlock();
    i2c1_on_error(status);
    return (int)status;
}

int i2c1_raw_read(uint8_t addr, uint8_t *buf, uint16_t len)
{
    HAL_StatusTypeDef status;

    if (addr > 0x7FU || buf == 0 || len == 0U) {
        return (int)HAL_ERROR;
    }
    i2c1_bus_lock();
    status = HAL_I2C_Master_Receive(&hi2c1, (uint16_t)(addr << 1),
                                    buf, len, 100U);
    i2c1_bus_unlock();
    i2c1_on_error(status);
    return (int)status;
}

int i2c1_probe(uint8_t addr)
{
    HAL_StatusTypeDef status;

    if (addr > 0x7FU) {
        return (int)HAL_ERROR;
    }
    i2c1_bus_lock();
    status = HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 1U, 20U);
    i2c1_bus_unlock();
    i2c1_on_error(status);
    return (int)status;
}
