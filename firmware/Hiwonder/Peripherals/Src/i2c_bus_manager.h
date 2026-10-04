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

/* Raw I2C transfer primitives (NO register address byte). These are required
 * for devices whose I2C interface is a plain byte stream, e.g. the PN532 NFC
 * module (its frames start directly with the 0x00 0x00 0xFF preamble). */
int i2c1_raw_write(uint8_t addr, const uint8_t *data, uint16_t len);
int i2c1_raw_read(uint8_t addr, uint8_t *buf, uint16_t len);

/* Probe whether a 7-bit device address ACKs on I2C1 (returns 0 = present). */
int i2c1_probe(uint8_t addr);

#ifdef __cplusplus
}
#endif

#endif /* __I2C_BUS_MANAGER_H_ */
