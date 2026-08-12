/**
 * @file key_porting.c
 * @author liang
 * @brief 板载按键接口实例及接口 
 * @version 1.0
 * @date 2025-06-23
 *
 * @copyright Copyright (c) 2023
 *
 */

#include "key.h"
#include "stm32f1xx.h" 
#include "gpio.h"



#define BUTTON_SCAN_PERIOD_MS 5

//  直接定义两个静态的按键对象实体，因为在编译时就已经分配好内存了。
static ButtonObjectTypeDef button1_object;
static ButtonObjectTypeDef button2_object;


ButtonObjectTypeDef* buttons[2] = {
    &button1_object,
    &button2_object
};
// -----------------------------------------------------------------


// 内部函数声明
static uint32_t button_read_pin(ButtonObjectTypeDef *self);

/**
 * @brief 按键初始化
 */
void buttons_init(void)
{
	
	for(int i = 0; i < 2; ++i) {
		button_object_init(buttons[i]);
		buttons[i]->id = i + 1;
	    buttons[i]->read_pin = button_read_pin;
		buttons[i]->combin_th = 300; 
		buttons[i]->lp_th = 1500;  
		buttons[i]->repeat_th = 400;
	}
}

/**
 * @brief 读取GPIO引脚电平
 * @note  KEY1->PD2, KEY2->PB3 
 * 按下时为低电平，所以用 异或1(^ 1) 将逻辑反转 (0->1, 1->0)
 */
static uint32_t button_read_pin(ButtonObjectTypeDef *self)
{
    switch(self->id) {
        case 1:
            return ((uint32_t)HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin)) ^ 1;
        case 2:
            return ((uint32_t)HAL_GPIO_ReadPin(KEY2_GPIO_Port, KEY2_Pin)) ^ 1;
        default:
            return 0;
    }
}

/**
 * @brief 按键周期处理任务 (由SysTick定时调用)
 * @note  此函数负责驱动按键状态机，处理消抖、长按、连击等。
 */
void button_periodic_task(void)
{
	button_task_handler(buttons[0], BUTTON_SCAN_PERIOD_MS);
	button_task_handler(buttons[1], BUTTON_SCAN_PERIOD_MS);
}
void my_button_callback(ButtonObjectTypeDef *self, ButtonEventIDEnum event)
{
  /* 这是一个空函数，里面啥也没有 */
}