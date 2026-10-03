#include "i2c_bus_manager.h"
#include "i2c.h"
#include "main.h"

static volatile bool g_i2c1_locked = false;

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
