// <<< Use Configuration Wizard in Context Menu >>>
#ifndef __GLOBAL_H__
#define __GLOBAL_H__

#include "main.h"
#include <string.h>
#include "led.h"

// 宏函数 获得A的低八位
#define GET_LOW_BYTE(A) ((uint8_t)(A))
// 宏函数 获得A的高八位
#define GET_HIGH_BYTE(A) ((uint8_t)((A) >> 8))
// 宏函数 将高地八位合成为十六位
#define BYTE_TO_HW(A, B) ((((uint16_t)(A)) << 8) | (uint8_t)(B))

/* 单位：ms */
#define LED_HANDLER_PERIOD       	LED_TIMER_PERIOD
#define BUZZER_HANDLER_PERIOD       25
#define SOFTWARE_VERSION			1

#define BUTTON_TASK_PERIOD  30u /* 板载按键扫描间隔 */

#if (ARM_SELECT == 0)
	#define SERVO_TYPE			1
#elif (ARM_SELECT == 1)
	#define SERVO_TYPE			2
#endif

// <o>Control Mode
//  <i>Select control mode
//  <0=> PC Control
//  <1=> Bluetooth Control
#define CONTROL_MODE		1
#if (CONTROL_MODE == 0)
	#define PC_CONTROL
#elif (CONTROL_MODE == 1)
	#define BLUETOOTH_CONTROL	
#endif

// <o>Chassis Select
//  <i>Select chassis type
//  <0=> None
//  <1=> Mecanum Chassis
//  <2=> Differential Chassis
#define CHASSIS_SELECT				1
#if (CHASSIS_SELECT == 1)
	#define MECANUM_CHASSIS
#elif (CHASSIS_SELECT == 2)
	#define DIFFERENTIAL_CHASSIS
#endif

#define KEY_NUM_OF_BUTTONS          2

// <o> 消抖时间 (Debounce Time in ms)
// <i> 按键消抖需要的时间，单位：毫秒
#define KEY_DEBOUNCE_MS             20

// <o> 长按触发时间 (Long Press Time in ms)
// <i> 按下持续多久后被识别为长按，单位：毫秒
#define KEY_LONGPRESS_MS            700

// <o> 长按重复触发周期 (Long Press Repeat Period in ms)
// <i> 进入长按后，每隔多久重复触发一次事件，单位：毫秒
#define KEY_LONGPRESS_REPEAT_MS     200

// <o> 连击超时时间 (Multi-click Timeout in ms)
// <i> 两次点击之间的最大间隔，超过该间隔则连击结束，单位：毫秒
#define KEY_MULTI_CLICK_TIMEOUT_MS  300
// ====================================================================

extern uint8_t arm_select;

#endif
// <<< end of configuration section >>>

