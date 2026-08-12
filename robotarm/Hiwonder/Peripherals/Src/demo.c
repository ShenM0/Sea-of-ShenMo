/**
 * @file key_app.c
 * @brief 按键应用层代码，封装按键初始化和处理逻辑
 */
#include "key.h"
#include "stdio.h"
#include "main.h"
// 定义标志位，用于在中断中设置，在主循环中处理
volatile uint8_t key1_click_flag = 0;
volatile uint8_t key1_double_click_flag = 0;
volatile uint8_t key1_long_press_flag = 0;
volatile uint8_t key2_click_flag = 0;
volatile uint8_t key2_double_click_flag = 0;
volatile uint8_t key2_long_press_flag = 0;

/**
 * @brief 按键事件处理函数
 * @param self 触发事件的按钮对象
 * @param event 按钮事件
 */
static void button_event_handler(ButtonObjectTypeDef *self, ButtonEventIDEnum event)
{
    switch(event) {
        case BUTTON_EVENT_CLICK:
            if(self->id == 1) {
                key1_click_flag = 1;
                printf("按键1被单击\r\n");
            } else if(self->id == 2) {
                key2_click_flag = 1;
                printf("按键2被单击\r\n");
            }
            break;
            
        case BUTTON_EVENT_DOUBLE_CLICK:
            if(self->id == 1) {
                key1_double_click_flag = 1;
                printf("按键1被双击\r\n");
            } else if(self->id == 2) {
                key2_double_click_flag = 1;
                printf("按键2被双击\r\n");
            }
            break;
            
        case BUTTON_EVENT_LONGPRESS:
            if(self->id == 1) {
                key1_long_press_flag = 1;
                printf("按键1被长按\r\n");
            } else if(self->id == 2) {
                key2_long_press_flag = 1;
                printf("按键2被长按\r\n");
            }
            break;
            
        default:
            break;
    }
}

/**
 * @brief 按键系统初始化和配置
 * @note 该函数只需在main中调用一次
 */
void key_system_init(void)
{
    // 初始化按键系统
    buttons_init();
    
    // 注册回调函数
    button_register_callback(buttons[0], button_event_handler);
    button_register_callback(buttons[1], button_event_handler);
    
    printf("按键系统初始化完成\r\n");
}

/**
 * @brief 检查并处理按键事件
 * @note 可选的函数，如果希望在主循环中进行其他处理
 */
void key_process(void)
{
    // 如果需要在按键回调之外进行额外处理，可以在这里添加代码
    // 例如，清除标志位或执行依赖于主循环状态的操作
    
    if (key1_click_flag) {
        // 额外的按键1单击处理
        key1_click_flag = 0;
    }
    
    if (key1_double_click_flag) {
        // 额外的按键1双击处理
        key1_double_click_flag = 0;
    }
    
    // ... 其他按键事件处理
}
