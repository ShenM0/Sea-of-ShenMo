#include "step_motor.h"

#include <math.h>

static const uint32_t step_pulse_high_ticks = 25U;

static volatile bool step_motor_continuous_mode = false;
static volatile uint32_t step_remain = 0;
static volatile int32_t step_pulse_count = 0;   // 累计脉冲数(带方向), 角度 = 计数值 * 0.05625
static volatile uint8_t step_current_dir = 0;

void step_motor_init(void)
{
    DL_GPIO_clearPins(STEP_MOTOR_DIR1_PORT, STEP_MOTOR_DIR1_PIN);
    DL_GPIO_setPins(STEP_MOTOR_EN1_PORT, STEP_MOTOR_EN1_PIN);
    NVIC_EnableIRQ(PWM1_INST_INT_IRQN);
}

// 方向控制
void step_motor_dir_set(uint8_t direction)
{
    step_current_dir = direction;
    if (direction == 0) {
        DL_GPIO_clearPins(STEP_MOTOR_DIR1_PORT, STEP_MOTOR_DIR1_PIN);
    } else {
        DL_GPIO_setPins(STEP_MOTOR_DIR1_PORT, STEP_MOTOR_DIR1_PIN);
    }
}

// 电机是否在运动 (连续模式运行中, 或定角度移动未完成)
bool step_motor_is_busy(void)
{
    return step_motor_continuous_mode || (step_remain != 0U);
}

// 当前相对零点的累计角度 (度), dir 1 方向为正
// 无原点开关: 上电位置即零点, 开环步进丢步会产生累积误差
float step_motor_get_angle(void)
{
    return (float) step_pulse_count * 0.05625f;
}

// 把当前位置重新设为零点 (需在电机停止时调用)
void step_motor_reset_angle(void)
{
    step_pulse_count = 0;
}

void step_motor_start(void)
{
    NVIC_EnableIRQ(PWM1_INST_INT_IRQN);
    DL_Timer_startCounter(PWM1_INST);
}

void step_motor_stop(void)
{
    step_motor_continuous_mode = false;
    step_remain = 0;
    DL_Timer_stopCounter(PWM1_INST);
}

// 角速度设置 角度/s
void step_set_speed(float speed)
{
    if (speed <= 0.0f) {
        return;
    }

    // 根据速度设置PWM频率
    uint32_t frequency = (uint32_t)(speed / 0.05625f); // 计算所需的PWM频率
    frequency = frequency > 0 ? frequency : 1;

    // 计算定时器溢出值
    uint32_t period = PWM1_INST_CLK_FREQ / frequency;
    period = period < 65536 ? period : 65535;
    period = period > 400 ? period : 400;   // 下限400: 脉冲频率上限12.5kHz (~700°/s)
    DL_Timer_setLoadValue(PWM1_INST, period);
    DL_Timer_setCaptureCompareValue(PWM1_INST,
                                    (period > step_pulse_high_ticks) ? step_pulse_high_ticks : 1U,
                                    GPIO_PWM1_C0_IDX);
}

void step_motor_set_signed_speed(float signed_speed)
{
    float abs_speed = fabsf(signed_speed);

    if (abs_speed < 0.5f) {
        step_motor_stop();
        return;
    }

    step_motor_dir_set((signed_speed > 0.0f) ? 1U : 0U);
    step_set_speed(abs_speed);
    step_motor_continuous_mode = true;
    step_remain = 0;
    step_motor_start();
}

void step_motor_set_angle(float angle)
{
    step_motor_continuous_mode = false;
    // 根据角度设置步数
    step_remain = (uint32_t)(angle / 0.05625); // 计算所需的步数
    step_motor_start();
}

void PWM1_INST_IRQHandler()
{
    switch (DL_Timer_getPendingInterrupt(PWM1_INST))
    {
    case DL_TIMER_IIDX_LOAD:
        {
            step_pulse_count += (step_current_dir != 0U) ? 1 : -1;
            if (step_motor_continuous_mode) {
                break;
            }
            if(step_remain == 0) {
                step_motor_stop();
                break;
            }
            step_remain --;
            break;
        }

    default:
        break;
    }
}
