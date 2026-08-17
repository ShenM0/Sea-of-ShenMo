/*
 * Servo Speed Control - Header
 * UART → Zigbee → MSPM0G3507 (S+xxx/S-xxx/S000)
 */

#ifndef _ARM_CONTROL_H_
#define _ARM_CONTROL_H_

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ARM_UART_PORT      UART_NUM_2
#define ARM_UART_TX_PIN    GPIO_NUM_11
#define ARM_UART_RX_PIN    GPIO_NUM_12
#define ARM_UART_BAUDRATE  115200
#define ARM_UART_BUF_SIZE  256

// Protocol: "S+050\n" (CCW), "S-030\n" (CW), "S000\n" (stop)

esp_err_t arm_uart_init(void);
esp_err_t arm_send_speed(int speed);
esp_err_t arm_send_command(const char *cmd);

#ifdef __cplusplus
}
#endif

#endif
