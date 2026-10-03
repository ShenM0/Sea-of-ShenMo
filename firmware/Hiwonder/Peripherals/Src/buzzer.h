/**
 * @file led.h
 * @author Lu Yongping (Lucas@hiwonder.com)
 * @brief 硬件无关的LED灯闪烁控制的函数即数据结构声明
 * @version 0.1
 * @date 2023-05-13
 *
 * @copyright Copyright (c) 2023
 *
 */

#ifndef __BUZZER_H_
#define __BUZZER_H_

#include <stdbool.h>
#include <stdint.h>


typedef struct BuzzerObjectObject BuzzerObjectTypeDef;
struct BuzzerObjectObject {
  uint32_t id;
  uint32_t ticks;     /**< @brief 毫秒计数，用于累计毫秒数切换状态机状态 */
  uint32_t ticks_on;  /**< @brief 周期内LED亮起时长，毫秒 */
  uint32_t ticks_off; /**< @brief 周期内LED熄灭时长，毫秒 */
	uint32_t freq;
	int stage;
	
  uint32_t ticks_on_set;
  uint32_t ticks_off_set;
	uint32_t freq_set;
  int32_t repeat_set;
  bool ticks_setted;
  int32_t repeat;

	
  int (*beep)(BuzzerObjectTypeDef *self, uint32_t freq, uint32_t ticks_on, uint32_t ticks_off, uint32_t repeat);
  void (*refresh)(BuzzerObjectTypeDef *self);
  void (*set_freq)(BuzzerObjectTypeDef *self, uint32_t freq);
  uint32_t (*get_ticks)(void);
};

typedef struct {
  void (*set_freq)(BuzzerObjectTypeDef *self, uint32_t freq);
  uint32_t (*get_ticks)(void);
} BuzzerObjectInitTypeDef;

/**
 * @brief 以默认数据初始化一个 LEDObjectTypeDef 实例的内存空间
 * @param self 要初始化的对象指针
 * @retval None.
 */
void buzzer_new(BuzzerObjectTypeDef *self, const BuzzerObjectInitTypeDef *init_config);

#endif
