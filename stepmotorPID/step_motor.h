#ifndef STEP_MOTOR_H
#define STEP_MOTOR_H

// ==================== MSPM0 → D36A (ATD5984) 接线 ====================
// 摆杆步进电机 (单路)
//   ST  (脉冲) : PA28 (TIMA1 CCP0) — 窄高脉冲, 频率控制转速
//   DIR (方向) : PA31              — 高=正向, 低=反向, 默认低
//   EN  (使能) : PB15              — 高=脱机, 低=使能, 初始化后置高
//
// D36A 驱动板 (轮趣科技 ATD5984) 仅需 ST/DIR/EN 三线
// ===================================================================

// 一脉冲 0.05625度
// 角速度 = 0.05625度 * 脉冲频率
// 脉冲频率 = 角速度 / 0.05625度
// 30角速度：30 / 0.05625 = 533.33Hz

#include <stdbool.h>

#include "ti_msp_dl_config.h"

void step_motor_init(void);
void step_motor_dir_set(uint8_t direction);
bool step_motor_is_busy(void);      // 电机是否在运动 (启动到位等待用)
float step_motor_get_angle(void);   // 相对零点的累计角度(度), 行程软限位用
void step_motor_reset_angle(void);  // 当前位置设为零点 (电机停止时调用)
void step_motor_start(void);
void step_motor_stop(void);
void step_set_speed(float speed);
void step_motor_set_signed_speed(float signed_speed);
void step_motor_set_angle(float angle);

#endif // STEP_MOTOR_H
