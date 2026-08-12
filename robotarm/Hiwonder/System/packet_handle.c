#include "led.h"
#include "packet.h"

#include "serial_servo.h" 
#include "buzzer.h"
#include "packet_reports.h"
#include "main.h"
extern LEDObjectTypeDef leds[2];
extern struct PacketController packet_controller;
//extern PWMServoObjectTypeDef pwm_servos[6];
extern SerialServoControllerTypeDef serial_servo_controller;
extern BuzzerObjectTypeDef buzzers[1];
//extern ws2812_controller_t ws2812;

#pragma pack(1)

/* 串口舵机 */
typedef struct {
  uint8_t cmd;
  uint8_t servo_id;
  uint8_t args[];
} SerialServoCommandTypeDef;

/* 串口舵机 */
typedef struct {
  uint8_t cmd;
  uint8_t servo_num;
  uint8_t args[];
} SerialServoMultiCommandTypeDef;
/* 串口舵机 */
typedef struct {
  uint8_t cmd;
  uint16_t duration;
  uint8_t servo_num;
  struct {
    uint8_t servo_id;
    uint16_t position;
  } elements[];
} SerialServoSetPositionCommandTypeDef;

/* PWM 舵机 */
typedef struct {
  uint8_t cmd;
  uint8_t servo_id;
  uint8_t args[];
} PWM_ServoCommandTypeDef;

typedef struct {
  uint8_t cmd;
  uint16_t duration;
  uint8_t servo_id;
  uint16_t pulse;
} PWM_ServoSetPositionCommandTypeDef;

typedef struct {
  uint8_t cmd;
  uint16_t duration;
  uint8_t servo_num;
  struct {
    uint8_t servo_id;
    uint16_t pulse;
  } elements[];
} PWMServoSetMultiPositionCommandTypeDef;
/* LED */

typedef struct {
  uint8_t led_id;
  uint16_t on_time;
  uint16_t off_time;
  uint16_t repeat;
} LedCommandTypeDef;

typedef struct {
  uint16_t freq;
  uint16_t on_time;
  uint16_t off_time;
  uint16_t repeat;
} BuzzerCommandTypeDef;

typedef struct {
  uint8_t cmd;
  uint8_t pixel_num;
  struct {
    uint8_t pixel_index;
    uint8_t r;
    uint8_t g;
    uint8_t b;
  } pixels[];
} RGBCommandTypeDef;

//电压报警值设置
typedef struct {
    uint8_t cmd;
    uint16_t limit;
} BatteryWarnTypeDef;

typedef struct {
  uint8_t sub_cmd;
  uint8_t success;
} PacketReportSysAckTypeDef;

typedef struct {
  uint8_t cmd;
  int16_t x_10;
  int16_t y_10;
  int16_t z_10;
  int16_t pitch_10;
  int16_t min_pitch_10;
  int16_t max_pitch_10;
  uint16_t move_time_ms;
  int16_t claw_open_angle_10;
  uint16_t claw_time_ms;
} ArmMoveAndClawCommandTypeDef;


#pragma pack()

static float clamp_float(float value, float min_value, float max_value)
{
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

#define ARM_RETURN_MOVE_TIME_MS 2500U
#define ARM_RETURN_CLAW_OPEN_ANGLE DEFAULT_CLAW_OPEN_ANGLE
#define ARM_RETURN_CLAW_TIME_MS 500U

typedef enum {
  ARM_DEFERRED_IDLE = 0,
  ARM_DEFERRED_WAIT_CLAW,
  ARM_DEFERRED_WAIT_RETURN,
  ARM_DEFERRED_WAIT_RELEASE,
} ArmDeferredStateEnum;

typedef struct {
  uint8_t active;
  ArmDeferredStateEnum state;
  uint32_t due_tick;
  float claw_open_angle;
  uint16_t claw_time_ms;
  uint16_t return_move_time_ms;
} ArmClawDeferredActionTypeDef;

static ArmClawDeferredActionTypeDef arm_claw_deferred_action = {0};

void packet_handle_task(void)
{
  if (!arm_claw_deferred_action.active) {
    return;
  }

  if ((int32_t)(get_ticks() - arm_claw_deferred_action.due_tick) < 0) {
    return;
  }

  switch (arm_claw_deferred_action.state) {
    case ARM_DEFERRED_WAIT_CLAW: {
      robot_arm_claw_set(arm_claw_deferred_action.claw_open_angle,
                         arm_claw_deferred_action.claw_time_ms);
      arm_claw_deferred_action.due_tick = get_ticks() + arm_claw_deferred_action.claw_time_ms + 20U;
      arm_claw_deferred_action.state = ARM_DEFERRED_WAIT_RETURN;
      break;
    }
    case ARM_DEFERRED_WAIT_RETURN: {
      robot_arm_go_home(arm_claw_deferred_action.return_move_time_ms);
      arm_claw_deferred_action.due_tick = get_ticks() + arm_claw_deferred_action.return_move_time_ms + 20U;
      arm_claw_deferred_action.state = ARM_DEFERRED_WAIT_RELEASE;
      break;
    }
    case ARM_DEFERRED_WAIT_RELEASE: {
      robot_arm_claw_set(ARM_RETURN_CLAW_OPEN_ANGLE,
                         ARM_RETURN_CLAW_TIME_MS);
      arm_claw_deferred_action.active = 0;
      arm_claw_deferred_action.state = ARM_DEFERRED_IDLE;
      break;
    }
    default: {
      arm_claw_deferred_action.active = 0;
      arm_claw_deferred_action.state = ARM_DEFERRED_IDLE;
      break;
    }
  }
}

/**
 * @brief 串口命令回调处理
 * @param frame 数据帧
 * @retval void
 */
static void packet_led_handle(struct PacketRawFrame *frame)
{
    LedCommandTypeDef *cmd = (LedCommandTypeDef*)frame->data_and_checksum;
    uint8_t led_id = cmd->led_id - 1;
    if(led_id < 2) { /* ID 都是从 1 开始 */
        led_flash(&leds[led_id], cmd->on_time, cmd->off_time, cmd->repeat);
    }
}


/**
 * @brief 串口命令回调处理
 * @param frame 数据帧
 * @retval void
 */
static void packet_buzzer_handle(struct PacketRawFrame *frame) {
//  led_flash(&leds[1], 10, 50, 1); 
  BuzzerCommandTypeDef *cmd = (BuzzerCommandTypeDef *)frame->data_and_checksum;
  buzzers[0].beep(&buzzers[0], cmd->freq, cmd->on_time, cmd->off_time, cmd->repeat);
}

static void packet_serial_servo_report_init(PacketReportSerialServoTypeDef *report, uint8_t servo_id, uint8_t cmd, int success) {
  report->servo_id = servo_id;
  report->sub_command = cmd;
  report->success = (uint8_t)((int8_t)success);
}

static void packet_serial_servo_handle(struct PacketRawFrame *frame) {
  PacketReportSerialServoTypeDef report;

  switch (frame->data_and_checksum[0]) {
  case 0x01: { /* 舵机控制 */
    SerialServoSetPositionCommandTypeDef *cmd = (SerialServoSetPositionCommandTypeDef *)frame->data_and_checksum;
    for (int i = 0; i < cmd->servo_num; i++) {
      serial_servo_set_position(&serial_servo_controller, cmd->elements[i].servo_id, cmd->elements[i].position, cmd->duration);
    }
    break;
  }
  case 0x03: { /* 停止舵机 */
    SerialServoMultiCommandTypeDef *cmd = (SerialServoMultiCommandTypeDef *)frame->data_and_checksum;
    for (int i = 0; i < cmd->servo_num; i++) {
      serial_servo_stop(&serial_servo_controller, cmd->args[i]);
    }
    break;
  }
  case 0x05: { /* 位置读取 */
    int16_t position = 0;
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd, serial_servo_read_position(&serial_servo_controller, cmd->servo_id, &position));
    memcpy(report.args, &position, 2);
    packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 5);
    break;
  }
  case 0x07: { /* 输入电压读取 */
    uint16_t vin = 0;
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd, serial_servo_read_vin(&serial_servo_controller, cmd->servo_id, &vin));
    memcpy(report.args, &vin, 2);
    packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 5);
    break;
  }
  case 0x09: { /* 温度读取 */
    uint8_t temp = 0;
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd, serial_servo_read_temp(&serial_servo_controller, cmd->servo_id, &temp));
    report.args[0] = temp;
    packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 4);
    break;
  }
  case 0x0B: { /* 卸载动力 */
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    serial_servo_load_unload(&serial_servo_controller, cmd->servo_id, 0);
    break;
  }
  case 0x0C: { /* 加载动力 */
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    serial_servo_load_unload(&serial_servo_controller, cmd->servo_id, 1);
    break;
  }
  case 0x0D: { /* 动力状态读取 */
    uint8_t load_unload;
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd, serial_servo_read_load_unload(&serial_servo_controller, cmd->servo_id, &load_unload));
    report.args[0] = load_unload;
    packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 4);
    break;
  }
  case 0x10: { /* ID 写入 */
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    serial_servo_set_id(&serial_servo_controller, cmd->servo_id, cmd->args[0]);
    break;
  }
  case 0x12: { /* ID 读取 */
    uint8_t servo_id;
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd, serial_servo_read_id(&serial_servo_controller, cmd->servo_id, &servo_id));
    report.args[0] = servo_id;
    packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 4);
    break;
  }
  case 0x20: { /* 偏差调整 */
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    serial_servo_set_deviation(&serial_servo_controller, cmd->servo_id, cmd->args[0]);
    break;
  }
  case 0x22: { /* 偏差读取 */
    int8_t dev = 0;
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd, serial_servo_read_deviation(&serial_servo_controller, cmd->servo_id, &dev));
    report.args[0] = (uint8_t)dev;
    packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 4);
    break;
  }
  case 0x24: { /* 偏差保存 */
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    serial_servo_save_deviation(&serial_servo_controller, cmd->servo_id);
    break;
  }
  case 0x30: { /* 位置限制设置 */
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    serial_servo_set_angle_limit(&serial_servo_controller, cmd->servo_id, *((uint16_t *)(&cmd->args[0])), *((uint16_t *)(&cmd->args[2])));
    break;
  }
  case 0x32: { /* 位置限制读取 */
    uint16_t limit[2] = {0};
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd, serial_servo_read_angle_limit(&serial_servo_controller, cmd->servo_id, limit));
    memcpy(&report.args, limit, 4);
    packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 7);
    break;
  }
  case 0x34: { /* 电压限制设置 */
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    serial_servo_set_vin_limit(&serial_servo_controller, cmd->servo_id, *((uint16_t *)(&cmd->args[0])), *((uint16_t *)(&cmd->args[2])));
    break;
  }
  case 0x36: { /* 电压限制读取 */
    uint16_t limit[2] = {0};
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd, serial_servo_read_vin_limit(&serial_servo_controller, cmd->servo_id, limit));
    memcpy(&report.args, limit, 4);
    packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 7);
    break;
  }
  case 0x38: { /* 温度限制设置 */
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    serial_servo_set_temp_limit(&serial_servo_controller, cmd->servo_id, cmd->args[0]);
    break;
  }
  case 0x3A: { /* 温度限制读取 */
    uint8_t limit = 0;
    SerialServoCommandTypeDef *cmd = (SerialServoCommandTypeDef *)frame->data_and_checksum;
    packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd, serial_servo_read_temp_limit(&serial_servo_controller, cmd->servo_id, &limit));
    report.args[0] = limit;
    packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 4);
    break;
  }

  default:
    break;
  }
}


static void packet_battery_limit_handle(struct PacketRawFrame *frame)
{
  BatteryWarnTypeDef *cmd = (BatteryWarnTypeDef*)frame->data_and_checksum;
    switch(frame->data_and_checksum[0]) {
        case 1: {
      if (frame->data_length >= sizeof(BatteryWarnTypeDef)) {
        change_battery_limit(cmd->limit);
      }
        }break;
    case 2: {
      PacketReportSysAckTypeDef report = {0};
      report.sub_cmd = 2;

      if (frame->data_length == sizeof(ArmMoveAndClawCommandTypeDef)) {
        ArmMoveAndClawCommandTypeDef *arm_cmd = (ArmMoveAndClawCommandTypeDef*)frame->data_and_checksum;
        float target_x = clamp_float((float)arm_cmd->x_10 / 10.0f, MIN_X, MAX_X);
        float target_y = clamp_float((float)arm_cmd->y_10 / 10.0f, MIN_Y, MAX_Y);
        float target_z = clamp_float((float)arm_cmd->z_10 / 10.0f, MIN_Z, MAX_Z);
        float pitch = clamp_float((float)arm_cmd->pitch_10 / 10.0f, -90.0f, 90.0f);
        float min_pitch = clamp_float((float)arm_cmd->min_pitch_10 / 10.0f, -90.0f, 90.0f);
        float max_pitch = clamp_float((float)arm_cmd->max_pitch_10 / 10.0f, -90.0f, 90.0f);
        float claw_open_angle = clamp_float((float)arm_cmd->claw_open_angle_10 / 10.0f, MIN_OPEN_ANGLE, MAX_OPEN_ANGLE);

        if (min_pitch > max_pitch) {
          float temp = min_pitch;
          min_pitch = max_pitch;
          max_pitch = temp;
        }

        if (arm_claw_deferred_action.active) {
          report.success = 0;
        } else {
          report.success = robot_arm_coordinate_set(target_x,
                                target_y,
                                target_z,
                                pitch,
                                min_pitch,
                                max_pitch,
                                (uint32_t)arm_cmd->move_time_ms);

          if (report.success) {
            arm_claw_deferred_action.active = 1;
            arm_claw_deferred_action.state = ARM_DEFERRED_WAIT_CLAW;
            arm_claw_deferred_action.due_tick = get_ticks() + arm_cmd->move_time_ms + 20U;
            arm_claw_deferred_action.claw_open_angle = claw_open_angle;
            arm_claw_deferred_action.claw_time_ms = arm_cmd->claw_time_ms;
            arm_claw_deferred_action.return_move_time_ms = ARM_RETURN_MOVE_TIME_MS;
          }
        }
      }

      packet_controller.transmit(&packet_controller,
                     PACKET_FUNC_SYS,
                     (uint8_t*)&report,
                     sizeof(report));
    }break;
        default:
            break;
    }
}


void packet_handle_init(void) {
	packet_controller.register_callback(&packet_controller, PACKET_FUNC_LED, packet_led_handle);
	//  packet_controller.register_callback(&packet_controller, PACKET_FUNC_PWM_SERVO, packet_pwm_servo_handle);
	packet_controller.register_callback(&packet_controller, PACKET_FUNC_BUS_SERVO, packet_serial_servo_handle);
	packet_controller.register_callback(&packet_controller, PACKET_FUNC_BUZZER, packet_buzzer_handle);
	packet_controller.register_callback(&packet_controller, PACKET_FUNC_SYS, packet_battery_limit_handle);
	//  packet_controller.register_callback(&packet_controller, PACKET_FUNC_RGB, packet_rgb_handle);
}
