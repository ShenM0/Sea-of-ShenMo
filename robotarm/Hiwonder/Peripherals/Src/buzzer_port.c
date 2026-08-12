/**
 * @file buzzer_port.c
 * @author ...
 * @brief 板载蜂鸣器实例及硬件接口适配层（基于 HAL 实现）。
 * @version 6.0 (HAL Rewritten)
 * @date 2025-06-19
 */

#include "buzzer.h"
#include "tim.h"    // 提供 htim4
#include "main.h"

BuzzerObjectTypeDef buzzers[1];

// 硬件层：获取系统滴答时间（单位 ms）
static uint32_t board_get_ticks(void) {
    return HAL_GetTick();
}

// 硬件层：设置蜂鸣器频率（基于 PWM 输出）
static void set_freq(BuzzerObjectTypeDef *self, uint32_t freq) {
    static uint32_t current_freq = 0;

    // 限制频率范围（保护蜂鸣器）
    if (freq > 20000) {
        freq = 20000;
    }

    if (current_freq != freq) {
        current_freq = freq;

        if (freq == 0) {
            // 设置比较值为0，关闭蜂鸣器输出
            __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0);
        } else {
            // 假设定时器输入频率为1MHz（预分频器配置：72MHz / 72 = 1MHz）
            uint32_t arr = 1000000 / freq;           // 自动重载值 ARR
            uint32_t ccr = arr / 2;                  // 占空比 50%
            
            // 停止计数器以更新 ARR 和 CCR
            __HAL_TIM_DISABLE(&htim4);

            __HAL_TIM_SET_AUTORELOAD(&htim4, arr - 1);
            __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, ccr - 1);

            // 启动计数器
            __HAL_TIM_ENABLE(&htim4);
        }
    }
}

// 初始化蜂鸣器系统
void buzzers_init(void) {
    // 启动定时器4的PWM通道3
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);

    BuzzerObjectInitTypeDef config = {
        .get_ticks = board_get_ticks,
        .set_freq = set_freq,
    };

    buzzer_new(&buzzers[0], &config);
}
