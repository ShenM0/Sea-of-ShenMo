/**
 * @file led_porting.c
 * @author liang
 * @brief 板载LED灯控制实例及接口实现 
 * @version 1.3
 * @date 2025-06-23
 * * @copyright Copyright (c) 2023
 * */

#include "led.h"
#include "main.h"      
#include <stdbool.h>   


#define LED_COUNT 2 


#define LED1_Pin        GPIO_PIN_15
#define LED1_GPIO_Port  GPIOB

LEDObjectTypeDef leds[LED_COUNT];


static LEDCtrlTypeDef ctrl_block_buffer[LED_COUNT];
static volatile bool has_new_ctrl_block[LED_COUNT] = {false};


static void led_set_pin(LEDObjectTypeDef *self, uint32_t level);       
static int put_ctrl_block(LEDObjectTypeDef *self, LEDCtrlTypeDef *p);  
static int get_ctrl_block(LEDObjectTypeDef *self, LEDCtrlTypeDef *p); 

/**
  * @brief 初始化所有led对象及硬件接口。
  * @retval None.
*/
void leds_init(void)
{
	led_object_init(&leds[0]);
	leds[0].id = 0; 
	leds[0].set_pin = led_set_pin;
	leds[0].get_ctrl_block = get_ctrl_block;
	leds[0].put_ctrl_block = put_ctrl_block;
    led_off(&leds[0]);

   led_object_init(&leds[1]);
	leds[1].id = 1; // ID为1
	leds[1].set_pin = led_set_pin;
	leds[1].get_ctrl_block = get_ctrl_block;
	leds[1].put_ctrl_block = put_ctrl_block;
  led_off(&leds[1]);
}

/**
  * @brief LED任务轮询函数
  * @detials 应在 main 函数的 while(1) 循环中被持续调用
  * @retval None.
*/
void leds_task_poll(void) 
{
    static uint32_t last_ticks = 0;
    uint32_t current_ticks = HAL_GetTick();
    uint32_t period = current_ticks - last_ticks;

    if (period > 0) {
        last_ticks = current_ticks;
        for(int i = 0; i < LED_COUNT; ++i) {
            led_task_handler(&leds[i], period);
        }
    }
}

/**
  * @brief 设置LED引脚的物理电平 (硬件相关)
  * @param self 指向被操作的LED对象，通过 self->id 区分
  * @param level 库传入的逻辑电平 (1=亮, 0=灭)
*/
static void led_set_pin(LEDObjectTypeDef *self, uint32_t level)
{
    GPIO_PinState pin_state = (GPIO_PinState)(level ^ 1u);
    switch(self->id)
    {
        case 0: // 控制ID为0的灯 (对应 leds[0])
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, pin_state);
            break;

        case 1: // 控制ID为1的灯 (对应 leds[1])
            HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, pin_state);
            break;
    }
}


/**
  * @brief 将控制命令放入对应LED的缓冲区
  * @retval 0 成功
*/
static int put_ctrl_block(LEDObjectTypeDef *self, LEDCtrlTypeDef *p) {
    if (self->id < LED_COUNT) {
        ctrl_block_buffer[self->id] = *p;
        has_new_ctrl_block[self->id] = true;
        return 0;
    }
	return -1; // 无效的ID
}

/**
  * @brief 从对应LED的缓冲区获取控制命令
  * @retval 0 成功获取新命令
  * @retval -1 没有新命令
*/
static int get_ctrl_block(LEDObjectTypeDef *self, LEDCtrlTypeDef *p) {
    if (self->id < LED_COUNT && has_new_ctrl_block[self->id]) {
        *p = ctrl_block_buffer[self->id];
        has_new_ctrl_block[self->id] = false;
        return 0; // 返回成功
    }
	return -1; // 没有新命令
}
