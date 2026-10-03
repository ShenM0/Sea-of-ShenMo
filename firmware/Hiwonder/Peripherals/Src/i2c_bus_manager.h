/* Filename: i2c_bus_manager.h */
#ifndef __I2C_BUS_MANAGER_H_
#define __I2C_BUS_MANAGER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "stdbool.h"
#include <stdint.h>

/**
 * @brief ���Ի�ȡ I2C1 ���ߵ�ʹ��Ȩ (����)
 * @note  �����������ռ�ã��˺�����������ֱ����ȡ�ɹ���
 */
void i2c1_bus_lock(void);

/**
 * @brief �ͷ� I2C1 ���ߵ�ʹ��Ȩ (����)
 */
void i2c1_bus_unlock(void);

int i2c1_write(uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);
int i2c1_read(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __I2C_BUS_MANAGER_H_ */
