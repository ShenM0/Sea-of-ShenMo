#ifndef __KEY_H__
#define __KEY_H__

#include "main.h" 
#include <stdio.h>

#define KEY_NUM_OF_BUTTONS          2

// 定义按键ID，方便用户调用
#define KEY1_ID                     0
#define KEY2_ID                     1

// 按键事件处理周期 (必须与 SysTick 中调用的周期一致！)
// SysTick 中是每 20ms 调用一次，所以这里是 20
#define KEY_TICK_PERIOD_MS          20

// 各类时间的阈值，单位：毫秒 (ms)
#define KEY_DEBOUNCE_MS             20      // 消抖时间
#define KEY_LONGPRESS_MS            700     // 长按触发时间
#define KEY_LONGPRESS_REPEAT_MS     200     // 长按重复触发周期
#define KEY_MULTI_CLICK_TIMEOUT_MS  300     // 连击之间的最大间隔时间

/**
 * @brief 按键事件ID枚举
 */
typedef enum {
    BUTTON_EVENT_PRESSED = 0x01,           /**< @brief 按钮被按下 (消抖后) */
    BUTTON_EVENT_LONGPRESS = 0x02,         /**< @brief 按钮被长按 */
    BUTTON_EVENT_LONGPRESS_REPEAT = 0x04,  /**< @brief 按钮长按重触发 */
    BUTTON_EVENT_RELEASE_FROM_LP = 0x08,   /**< @brief 按钮从长按中松开 */
    BUTTON_EVENT_CLICK = 0x20,             /**< @brief 按钮被单击 */
    BUTTON_EVENT_DOUBLE_CLICK = 0x40,      /**< @brief 按钮被双击 */
    BUTTON_EVENT_TRIPLE_CLICK = 0x80,      /**< @brief 按钮被三连击 */
} ButtonEventIDEnum;

/**
 * @brief 按键事件回调函数指针类型
 * @param button_id   触发事件的按键ID (例如 KEY1_ID)
 * @param event       发生的事件ID (ButtonEventIDEnum 中的一种)
 */
typedef void (*button_callback_t)(uint8_t button_id, ButtonEventIDEnum event);


/*===================================================================
 * 3. 用户API函数 (User API Functions)
 *==================================================================*/

/**
 * @brief  初始化所有按键
 * @param  None
 * @retval None
 */
void button_init(void);

/**
 * @brief  注册按键事件的回调函数
 * @param  button_id 要注册的按键ID
 * @param  callback  当事件发生时要调用的函数
 * @retval None
 */
void button_register_callback(uint8_t button_id, button_callback_t callback);

/**
 * @brief  按键周期处理函数
 * @note   必须被周期性调用，周期由 KEY_TICK_PERIOD_MS 决定
 * @param  None
 * @retval None
 */
void button_tick(void);


/*===================================================================
 * 4. 移植层接口 (Porting Interface)
 * @note   以下函数需要由 key_porting.c 文件实现
 *==================================================================*/

/**
 * @brief  [移植层接口] 读取指定按键的物理电平
 * @param  button_id 按键ID
 * @retval 0: 松开状态, 1: 按下状态
 */
extern uint8_t key_port_read_pin(uint8_t button_id);


#endif /* __KEY_H__ */

