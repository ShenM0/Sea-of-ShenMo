#include <stdint.h>
#include <string.h>

#include "buzzer.h"
#include "i2c_bus_manager.h"
#include "led.h"
#include "packet.h"
#include "packet_reports.h"
#include "serial_servo.h"

#define I2C_MAX_TRANSFER 48U
#define I2C_TIMEOUT_MS 100U

extern LEDObjectTypeDef leds[2];
extern BuzzerObjectTypeDef buzzers[1];
extern SerialServoControllerTypeDef serial_servo_controller;
extern struct PacketController packet_controller;

#pragma pack(1)
typedef struct {
    uint8_t cmd;
    uint8_t servo_id;
    uint8_t args[];
} SerialServoCommand;

typedef struct {
    uint8_t cmd;
    uint8_t servo_num;
    uint8_t servo_ids[];
} SerialServoMultiCommand;

typedef struct {
    uint8_t cmd;
    uint16_t duration;
    uint8_t servo_num;
    struct {
        uint8_t servo_id;
        uint16_t position;
    } elements[];
} SerialServoSetPositionCommand;

typedef struct {
    uint8_t led_id;
    uint16_t on_time;
    uint16_t off_time;
    uint16_t repeat;
} LedCommand;

typedef struct {
    uint16_t freq;
    uint16_t on_time;
    uint16_t off_time;
    uint16_t repeat;
} BuzzerCommand;
#pragma pack()

static void packet_led_handle(struct PacketRawFrame *frame)
{
    LedCommand cmd;
    uint8_t led_index;

    if (frame->data_length < sizeof(cmd)) {
        return;
    }
    memcpy(&cmd, frame->data_and_checksum, sizeof(cmd));
    if (cmd.led_id == 0U || cmd.led_id > 2U) {
        return;
    }
    led_index = (uint8_t)(cmd.led_id - 1U);
    led_flash(&leds[led_index], cmd.on_time, cmd.off_time, cmd.repeat);
}

static void packet_buzzer_handle(struct PacketRawFrame *frame)
{
    BuzzerCommand cmd;

    if (frame->data_length < sizeof(cmd)) {
        return;
    }
    memcpy(&cmd, frame->data_and_checksum, sizeof(cmd));
    buzzers[0].beep(&buzzers[0], cmd.freq, cmd.on_time, cmd.off_time, cmd.repeat);
}

static void packet_serial_servo_report_init(PacketReportSerialServoTypeDef *report,
                                            uint8_t servo_id,
                                            uint8_t cmd,
                                            int success)
{
    report->servo_id = servo_id;
    report->sub_command = cmd;
    report->success = (uint8_t)((int8_t)success);
}

static void packet_serial_servo_handle(struct PacketRawFrame *frame)
{
    PacketReportSerialServoTypeDef report = {0};
    SerialServoCommand *cmd;

    if (frame->data_length == 0U) {
        return;
    }
    cmd = (SerialServoCommand *)frame->data_and_checksum;

    switch (cmd->cmd) {
    case 0x01: {
        SerialServoSetPositionCommand *set_cmd;
        uint16_t expected_length;
        uint8_t i;

        if (frame->data_length < 4U) {
            return;
        }
        set_cmd = (SerialServoSetPositionCommand *)frame->data_and_checksum;
        expected_length = (uint16_t)(4U + set_cmd->servo_num * 3U);
        if (frame->data_length < expected_length) {
            return;
        }
        for (i = 0; i < set_cmd->servo_num; ++i) {
            serial_servo_set_position(&serial_servo_controller,
                                      set_cmd->elements[i].servo_id,
                                      set_cmd->elements[i].position,
                                      set_cmd->duration);
        }
        break;
    }
    case 0x03: {
        SerialServoMultiCommand *multi_cmd;
        uint8_t i;

        if (frame->data_length < 2U) {
            return;
        }
        multi_cmd = (SerialServoMultiCommand *)frame->data_and_checksum;
        if (frame->data_length < (uint8_t)(2U + multi_cmd->servo_num)) {
            return;
        }
        for (i = 0; i < multi_cmd->servo_num; ++i) {
            serial_servo_stop(&serial_servo_controller, multi_cmd->servo_ids[i]);
        }
        break;
    }
    case 0x05: {
        int16_t position = 0;
        if (frame->data_length < 2U) return;
        packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd,
            serial_servo_read_position(&serial_servo_controller, cmd->servo_id, &position));
        memcpy(report.args, &position, sizeof(position));
        packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 5U);
        break;
    }
    case 0x07: {
        uint16_t vin = 0;
        if (frame->data_length < 2U) return;
        packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd,
            serial_servo_read_vin(&serial_servo_controller, cmd->servo_id, &vin));
        memcpy(report.args, &vin, sizeof(vin));
        packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 5U);
        break;
    }
    case 0x09: {
        uint8_t temp = 0;
        if (frame->data_length < 2U) return;
        packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd,
            serial_servo_read_temp(&serial_servo_controller, cmd->servo_id, &temp));
        report.args[0] = temp;
        packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 4U);
        break;
    }
    case 0x0B:
    case 0x0C:
        if (frame->data_length < 2U) return;
        serial_servo_load_unload(&serial_servo_controller, cmd->servo_id, cmd->cmd == 0x0CU);
        break;
    case 0x0D: {
        uint8_t load_unload = 0;
        if (frame->data_length < 2U) return;
        packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd,
            serial_servo_read_load_unload(&serial_servo_controller, cmd->servo_id, &load_unload));
        report.args[0] = load_unload;
        packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 4U);
        break;
    }
    case 0x10:
        if (frame->data_length < 3U) return;
        serial_servo_set_id(&serial_servo_controller, cmd->servo_id, cmd->args[0]);
        break;
    case 0x12: {
        uint8_t servo_id = 0;
        if (frame->data_length < 2U) return;
        packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd,
            serial_servo_read_id(&serial_servo_controller, cmd->servo_id, &servo_id));
        report.args[0] = servo_id;
        packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 4U);
        break;
    }
    case 0x20:
        if (frame->data_length < 3U) return;
        serial_servo_set_deviation(&serial_servo_controller, cmd->servo_id, cmd->args[0]);
        break;
    case 0x22: {
        int8_t deviation = 0;
        if (frame->data_length < 2U) return;
        packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd,
            serial_servo_read_deviation(&serial_servo_controller, cmd->servo_id, &deviation));
        report.args[0] = (uint8_t)deviation;
        packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 4U);
        break;
    }
    case 0x24:
        if (frame->data_length < 2U) return;
        serial_servo_save_deviation(&serial_servo_controller, cmd->servo_id);
        break;
    case 0x30:
        if (frame->data_length < 6U) return;
        serial_servo_set_angle_limit(&serial_servo_controller, cmd->servo_id,
                                     *((uint16_t *)&cmd->args[0]), *((uint16_t *)&cmd->args[2]));
        break;
    case 0x32: {
        uint16_t limits[2] = {0};
        if (frame->data_length < 2U) return;
        packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd,
            serial_servo_read_angle_limit(&serial_servo_controller, cmd->servo_id, limits));
        memcpy(report.args, limits, sizeof(limits));
        packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 7U);
        break;
    }
    case 0x34:
        if (frame->data_length < 6U) return;
        serial_servo_set_vin_limit(&serial_servo_controller, cmd->servo_id,
                                   *((uint16_t *)&cmd->args[0]), *((uint16_t *)&cmd->args[2]));
        break;
    case 0x36: {
        uint16_t limits[2] = {0};
        if (frame->data_length < 2U) return;
        packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd,
            serial_servo_read_vin_limit(&serial_servo_controller, cmd->servo_id, limits));
        memcpy(report.args, limits, sizeof(limits));
        packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 7U);
        break;
    }
    case 0x38:
        if (frame->data_length < 3U) return;
        serial_servo_set_temp_limit(&serial_servo_controller, cmd->servo_id, cmd->args[0]);
        break;
    case 0x3A: {
        uint8_t limit = 0;
        if (frame->data_length < 2U) return;
        packet_serial_servo_report_init(&report, cmd->servo_id, cmd->cmd,
            serial_servo_read_temp_limit(&serial_servo_controller, cmd->servo_id, &limit));
        report.args[0] = limit;
        packet_controller.transmit(&packet_controller, PACKET_FUNC_BUS_SERVO, (uint8_t *)&report, 4U);
        break;
    }
    default:
        break;
    }
}

static void packet_i2c_handle(struct PacketRawFrame *frame)
{
    uint8_t *data = frame->data_and_checksum;
    uint8_t response[4U + I2C_MAX_TRANSFER];
    uint8_t address;
    uint8_t reg;
    uint8_t response_length;
    int result;

    if (frame->data_length < 1U) return;
    if (data[0] == 0x01U) {
        if (frame->data_length < 4U) return;
        address = data[1];
        reg = data[2];
        result = i2c1_write(address, reg, &data[3], (uint16_t)(frame->data_length - 3U));
        response[0] = 0x01U;
        response[1] = address;
        response[2] = reg;
        response[3] = (uint8_t)result;
        packet_controller.transmit(&packet_controller, PACKET_FUNC_I2C, response, 4U);
    } else if (data[0] == 0x02U) {
        uint8_t length;
        if (frame->data_length < 4U) return;
        address = data[1];
        reg = data[2];
        length = data[3];
        if (length == 0U || length > I2C_MAX_TRANSFER) return;
        result = i2c1_read(address, reg, &response[4], length);
        response[0] = 0x02U;
        response[1] = address;
        response[2] = reg;
        response[3] = (uint8_t)result;
        response_length = (uint8_t)(4U + (result == 0 ? length : 0U));
        packet_controller.transmit(&packet_controller, PACKET_FUNC_I2C, response, response_length);
    }
}

void packet_handle_init(void)
{
    packet_controller.register_callback(&packet_controller, PACKET_FUNC_LED, packet_led_handle);
    packet_controller.register_callback(&packet_controller, PACKET_FUNC_BUS_SERVO, packet_serial_servo_handle);
    packet_controller.register_callback(&packet_controller, PACKET_FUNC_BUZZER, packet_buzzer_handle);
    packet_controller.register_callback(&packet_controller, PACKET_FUNC_I2C, packet_i2c_handle);
}
