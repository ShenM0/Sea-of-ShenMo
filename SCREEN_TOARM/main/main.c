/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"
#include "arm_control.h"

// Declare the UI creation function (defined in arm_control_ui.c)
void arm_control_ui_create(void);

void app_main()
{
    waveshare_esp32_s3_rgb_lcd_init(); // Initialize the Waveshare ESP32-S3 RGB LCD
    // wavesahre_rgb_lcd_bl_on();  //Turn on the screen backlight
    // wavesahre_rgb_lcd_bl_off(); //Turn off the screen backlight

    // Initialize the Zigbee wireless serial UART for arm communication
    arm_uart_init();

    ESP_LOGI(TAG, "Starting Servo Control");
    if (lvgl_port_lock(-1)) {
        arm_control_ui_create(); // Create the arm control interface
        lvgl_port_unlock();
    }
}
