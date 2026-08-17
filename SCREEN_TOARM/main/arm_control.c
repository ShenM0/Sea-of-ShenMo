/*
 * Servo Speed Control - UART Module
 * MSPM0 protocol: S+xxx (CCW), S-xxx (CW), S000 (stop)
 */

#include <string.h>
#include <stdio.h>
#include "arm_control.h"

static const char *TAG = "servo_uart";
static bool s_uart_ready = false;

esp_err_t arm_uart_init(void)
{
    ESP_LOGI(TAG, "UART%d init TX:%d RX:%d %d baud",
             ARM_UART_PORT, ARM_UART_TX_PIN, ARM_UART_RX_PIN, ARM_UART_BAUDRATE);

    uart_config_t uart_config = {
        .baud_rate  = ARM_UART_BAUDRATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(ARM_UART_PORT, ARM_UART_BUF_SIZE * 2,
                                        ARM_UART_BUF_SIZE * 2, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(ARM_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ARM_UART_PORT, ARM_UART_TX_PIN, ARM_UART_RX_PIN,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    s_uart_ready = true;
    ESP_LOGI(TAG, "UART ready");
    return ESP_OK;
}

esp_err_t arm_send_command(const char *cmd)
{
    if (!s_uart_ready) return ESP_FAIL;
    if (!cmd) return ESP_ERR_INVALID_ARG;

    int len = strlen(cmd);
    uart_write_bytes(ARM_UART_PORT, cmd, len);
    if (len == 0 || cmd[len-1] != '\n')
        uart_write_bytes(ARM_UART_PORT, "\n", 1);

    ESP_LOGI(TAG, "Sent: %s", cmd);
    return ESP_OK;
}

esp_err_t arm_send_speed(int speed)
{
    /* MSPM0 expects: S+050 (CCW), S-030 (CW), S000 (stop) */
    char cmd[16];
    if (speed > 0)
        snprintf(cmd, sizeof(cmd), "S+%03d", speed);
    else if (speed < 0)
        snprintf(cmd, sizeof(cmd), "S-%03d", -speed);
    else
        snprintf(cmd, sizeof(cmd), "S000");
    return arm_send_command(cmd);
}
