/*
 * Copyright (c) 2021, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * 车载平衡滚球运动控制系统 (H题) — 最小驱动验证
 *
 * 架构: 接收与控制解耦 + 串级位置控制
 *   - 串口 (UART0 115200, PB0=TX/PB1=RX, 收发全非阻塞) 只负责更新
 *     "最新反馈值", 帧来得快时旧帧被新帧覆盖 (最新值语义, 不会丢控制)
 *   - 外环 (球位置 PD): 球偏差 -> 目标摆杆倾角, 钳位 ±CASCADE_TILT_MAX_DEG
 *   - 内环 (位置伺服): 目标倾角 -> 电机角速度, 电机角度=步进脉冲计数(精确反馈)
 *     步进是位置执行器, 内环无积分漂移, 摆幅由外环钳位直接决定
 *   - 固定节拍 (~1ms) 连续运行, 与相机帧率无关
 *   - 超过 VISION_TIMEOUT_LOOPS 拍未收到新帧则自动停电机 (丢帧保护)
 *
 * 运行模式: 相机收到 "four" 后逐帧发送小球相对中心 O 的偏差 (单位 cm),
 *           帧格式 "$data,checksum\n" (如 "$-3.5000,215\n", checksum=data字节和%256),
 *           单片机闭环稳球在中点; 上电后每 ~1s 重发启动命令直到收到首帧
 * 相机反馈: 题目四状态下持续发送 (丢球时发 0.0000), 超时无新帧自动停电机
 * 行程保护: 启动位置为 0 度, 目标角只允许在 MIN~MAX 之间
 * 按键启动: KEY_21 (PB21, 下拉, 上升沿中断) 按下手动向相机发送启动命令
 *
 * 注意: UART 与相机直连, 不要向串口打印调试信息, 以免干扰相机解析
 */

#include "ti_msp_dl_config.h"
#include "delay.h"
#include "step_motor.h"
#include "uart.h"

#include <math.h>

#define CONTROL_LOOP_DELAY_MS      1U       // 控制节拍 ~1ms
#define CONTROL_LOOP_DT_S          0.0012f  // 控制回路实测周期 (~1ms 延时 + 处理耗时), 积分时间尺度/帧间隔换算用

// ---- 外环: 球位置 PD -> 目标摆杆倾角 ----
#define VISION_DEADBAND            0.10f //0.20   // 球偏差死区(cm): 死区内目标倾角=0(摆杆回水平); 相机噪声~0.16cm, 必须高于噪声否则一直追噪声抖动
#define VISION_FILTER_ALPHA        0.30f    // 误差滤波旧值权重 (每拍): 调小跟踪更快, 调大更平滑
#define CASCADE_KP_TILT            0.30f    // 外环P: 目标摆杆倾角(度)/球偏差(cm) —— 响应速度主项: 纠正太慢调大, 稳态来回冲调小
#define CASCADE_KI_TILT            0.2f     // 外环I(度/(cm·s)): 积分自学习水平零位, 0=关闭用手动补偿
#define CASCADE_KD_TILT            0.6f     // 外环D: 目标摆杆倾角(度)/球速(cm/s) —— 阻尼主项: 超调振荡调大, 动作迟钝发黏调小
#define CASCADE_TILT_MAX_DEG       1.5f     // 目标摆杆倾角限幅(±度): 摆幅钳位, 直接决定最大纠正力度; 大偏差纠正慢调大, 太猛调小
#define CASCADE_I_LIMIT            3.0f     // 外环积分限幅(cm·s): 最大学习补偿=KI×I_LIMIT=0.6°
#define MOTOR_DEG_PER_BEAM_DEG     7.3f     // 电机角/摆杆角 (5cm曲柄@~47°工作点, 5cm连杆, 25cm摆杆)
#define BEAM_LEVEL_OFFSET_DEG      0.0f     // 水平零位手动补偿(电机角), 积分自学习开启时保持0
// ---- 内环: 电机角度位置伺服 (步进脉冲计数=精确位置反馈, 无积分漂移) ----
#define CASCADE_KP_INNER           150.0f   // 内环P: 电机角速度(°/s)/电机角误差(°), 跟踪太慢调大
#define CASCADE_MAX_SPEED_DPS      200.0f   // 内环速度上限: 摆角到位速度, 响应慢调大(脉冲上限~700°/s), 太猛调小
#define CASCADE_INNER_DEADZONE     0.12f    // 内环到位死区(电机角度): 必须大于1个脉冲步距0.056°, 否则来回单步振荡(抖动)
#define VISION_TIMEOUT_LOOPS       100000U     // ~200ms 无新帧停电机
#define VISION_X_DIRECTION_SIGN   (1.0f)
#define VISION_TRAVEL_MIN_DEG      -15.0f   // 目标角下限(电机角, 抬升为负): 曲柄最多到67°(摆杆约+1.7°)
#define VISION_TRAVEL_MAX_DEG      15.0f     // 目标角上限: 启动位置为0度; 需要下探(往低位倾)时改成 +10
#define VISION_MAX_ABS_CM          20.0f    // 反馈值合法范围, 超出视为畸形帧丢弃
#define VISION_START_CMD           "three\r\n"  // 相机端启动命令
#define CAMERA_CMD_RESEND_LOOPS    1000U    // 收到首帧前 ~1s 重发一次 (相机启动比单片机慢, 只发一次会错过)
#define STARTUP_GOTO_ANGLE_DEG     26.0f    // 上电从最低位转到启动位置的角度 (0=不转)
#define STARTUP_GOTO_DIR           0U       // 抬升方向: dir0=0, dir1=1 (若上电是顶着最低位撞/咔咔丢步, 改成1)
#define STARTUP_GOTO_SPEED_DPS     30.0f    // 转到启动位置的速度 (到位由 is_busy 判定, 不需调延时)



typedef struct {
	float integral;
	float prev_error;
	float filtered_error;
	float derivative;      // 微分低通值 (cm/s), 仅来新帧时按真实帧间隔更新
} vision_axis_controller_t;

static float clampf_local(float value, float min_value, float max_value)
{
	if (value < min_value) {
		return min_value;
	}
	if (value > max_value) {
		return max_value;
	}
	return value;
}

static void vision_axis_reset(vision_axis_controller_t *controller)
{
	controller->integral = 0.0f;
	controller->prev_error = 0.0f;
	controller->filtered_error = 0.0f;
	controller->derivative = 0.0f;
}

// 外环: 球位置 PD -> 目标电机角度 (度, 相对启动零位)
// 积分采用条件积分: 只在死区外累积, 死区内保持 (KI 可自动学习水平零位偏差)
// 目标摆杆倾角钳位 ±CASCADE_TILT_MAX_DEG 即摆幅钳位
// 微分只在来新帧时按真实帧间隔更新: 若每 1ms 拍都除以 1ms dt, 帧到达瞬间的
// 误差跳变会被放大约 25 倍 (帧间隔~30ms/拍间隔1.2ms), 每帧一个目标尖峰 -> 抖动
static float vision_outer_step(vision_axis_controller_t *controller, float raw_error,
							   uint8_t new_frame, float frame_dt_s)
{
	float tilt_target;

	controller->filtered_error =
		controller->filtered_error * VISION_FILTER_ALPHA +
		raw_error * (1.0f - VISION_FILTER_ALPHA);

	if (new_frame != 0U) {
		// 微分按真实帧间隔 (cm/s), 再低通压帧噪声; 两帧之间保持上次值
		float raw_derivative = (controller->filtered_error - controller->prev_error) / frame_dt_s;
		controller->derivative = controller->derivative * 0.5f + raw_derivative * 0.5f;
		controller->prev_error = controller->filtered_error;
	}

	if (fabsf(controller->filtered_error) > VISION_DEADBAND) {
		// 条件积分: 只在死区外累积, 防相机噪声 windup (时间尺度 cm·s)
		controller->integral += controller->filtered_error * CONTROL_LOOP_DT_S;
		controller->integral = clampf_local(controller->integral,
											-CASCADE_I_LIMIT,
											CASCADE_I_LIMIT);
		tilt_target =
			CASCADE_KP_TILT * controller->filtered_error +
			CASCADE_KI_TILT * controller->integral +
			CASCADE_KD_TILT * controller->derivative;
		tilt_target = clampf_local(tilt_target,
								   -CASCADE_TILT_MAX_DEG,
								   CASCADE_TILT_MAX_DEG);
	} else {
		// 死区内: P/D 归零, 仅保留积分维持水平零位补偿 (KI=0 时目标=0)
		tilt_target = CASCADE_KI_TILT * controller->integral;
	}

	return tilt_target * MOTOR_DEG_PER_BEAM_DEG * VISION_X_DIRECTION_SIGN;
}

// ---- 按键 KEY_21 (PB21, 下拉输入, 按下=上升沿): 中断只置标志, 主循环消抖后发启动命令 ----
// static volatile uint8_t g_key_start_req = 0;

// void GPIOB_IRQHandler(void)
// {
// 	if (DL_GPIO_getPendingInterrupt(GPIO_KEY_PORT) == DL_GPIO_IIDX_DIO21) {
// 		g_key_start_req = 1;
// 	}
// }

int main(void)
{
    SYSCFG_DL_init();

	uint32_t tick = 0;
	uint32_t last_frame_tick = 0;
	uint32_t frame_dt_loops = 1;     // 距上帧的拍数, 微分真实帧间隔用
	uint32_t cmd_resend_count = 0;
	uint8_t camera_started = 0;
	uint8_t motor_active = 0;
	uint8_t new_frame = 0;
	float latest_feedback = 0.0f;
	float new_feedback = 0.0f;
	vision_axis_controller_t x_controller = {0};

	step_motor_init();
	UART_vision_init();
	step_motor_set_signed_speed(0.0f);

	// 上电后从最低位单向转到启动位置: 抬升方向转 STARTUP_GOTO_ANGLE_DEG 度
	// 必须等移动真正完成再清零: 固定延时不随角度/速度变化, 没走完就清零
	// 会让零点落在行程中间 (下限位置不更新), 还会带着错误基准进闭环
	if (STARTUP_GOTO_ANGLE_DEG > 0.0f)
	{
		step_set_speed(STARTUP_GOTO_SPEED_DPS);
		step_motor_dir_set(STARTUP_GOTO_DIR);
		step_motor_set_angle(STARTUP_GOTO_ANGLE_DEG);
		while (step_motor_is_busy()) {
		}
	}
	step_motor_reset_angle();   // 启动位置作为行程零点 (下限)

	while(1)
	{
		UART_tx_poll();   // 非阻塞排空 TX 队列

		// 按键启动: 中断标志 + 20ms 消抖确认仍按下, 向相机发启动命令 (非阻塞入队)
		// if (g_key_start_req != 0U) {
		// 	g_key_start_req = 0;
		// 	delay_ms(20U);
		// 	if (DL_GPIO_readPins(GPIO_KEY_PORT, GPIO_KEY_KEY_21_PIN) != 0U) {
		// 		UART_send_string(VISION_START_CMD);   // 队列满则丢弃, 不阻塞
		// 	}
		// }

		// 接收: 有新帧则更新最新反馈值 (旧帧被覆盖是最新值语义, 非丢失)
		// 超差帧直接丢弃: 丢字节拼出的畸形行可能被 sscanf 解析成合法但错误的数
		if (UART_try_get_feedback(&new_feedback) &&
		    (fabsf(new_feedback) <= VISION_MAX_ABS_CM))
		{
			camera_started = 1;
			last_frame_tick = tick;
			latest_feedback = new_feedback;
			new_frame = 1;
		}


		// 控制: 串级 — 外环球位置PD出目标摆角, 内环电机角度伺服跟踪, 固定节拍 ~1ms
		// 调试: 无新帧停机保护已临时注释 (原为 (tick - last_frame_tick) < VISION_TIMEOUT_LOOPS)
		if (camera_started != 0U)
		{
			float beam_angle = step_motor_get_angle();
			float frame_dt_s = (float)(frame_dt_loops != 0U ? frame_dt_loops : 1U) * CONTROL_LOOP_DT_S;
			float motor_target = vision_outer_step(&x_controller, latest_feedback,
												   new_frame, frame_dt_s) + BEAM_LEVEL_OFFSET_DEG;
			float angle_err;
			float x_speed;

			// 目标角行程钳位: 不许出软限位, 内环伺服自动只在界内走
			motor_target = clampf_local(motor_target, VISION_TRAVEL_MIN_DEG, VISION_TRAVEL_MAX_DEG);

			// 内环位置伺服: 角误差 -> 角速度, 进到位死区即停
			angle_err = motor_target - beam_angle;
			if (fabsf(angle_err) < CASCADE_INNER_DEADZONE) {
				x_speed = 0.0f;
			} else {
				x_speed = clampf_local(CASCADE_KP_INNER * angle_err,
									   -CASCADE_MAX_SPEED_DPS,
									   CASCADE_MAX_SPEED_DPS);
			}

			motor_active = 1;
			step_motor_set_signed_speed(x_speed);
		}
		else if (motor_active != 0U)
		{
			motor_active = 0;
			vision_axis_reset(&x_controller);
			step_motor_set_signed_speed(0.0f);
		}

		// 上电自动触发: 收到首帧前每 ~1s 重发一次 "four", 不收到帧就一直发
		// (相机启动比单片机慢数秒, 只发一次会错过; 收到首帧说明相机已就绪)
		if (camera_started == 0U)
		{
			cmd_resend_count++;
			if (cmd_resend_count >= CAMERA_CMD_RESEND_LOOPS)
			{
				cmd_resend_count = 0;
				UART_send_string(VISION_START_CMD);   // 队列满则下轮重试, 不阻塞
			}
		}

		tick++;
		if (new_frame != 0U) {
			new_frame = 0;
			frame_dt_loops = 0;
		}
		frame_dt_loops++;
		delay_ms(CONTROL_LOOP_DELAY_MS);
	}
}