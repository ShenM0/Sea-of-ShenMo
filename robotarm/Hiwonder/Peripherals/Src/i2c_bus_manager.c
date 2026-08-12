#include "i2c_bus_manager.h"
#include "main.h"

static volatile bool g_i2c1_locked = false;

void i2c1_bus_lock(void)
{
    while (g_i2c1_locked)
    {
			//里面没有什么东西只是在空转
    }
    
    // 禁用中断
    __disable_irq();
    // 获取锁
    g_i2c1_locked = true;
    // 重新使能中断
    __enable_irq();
}

void i2c1_bus_unlock(void)
{
    // 释放锁
    g_i2c1_locked = false;
}
