/* Filename: i2c_bus_manager.h */
#ifndef __I2C_BUS_MANAGER_H_
#define __I2C_BUS_MANAGER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "stdbool.h"

/**
 * @brief 尝试获取 I2C1 总线的使用权 (上锁)
 * @note  如果总线正被占用，此函数会阻塞，直到获取成功。
 */
void i2c1_bus_lock(void);

/**
 * @brief 释放 I2C1 总线的使用权 (解锁)
 */
void i2c1_bus_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* __I2C_BUS_MANAGER_H_ */
