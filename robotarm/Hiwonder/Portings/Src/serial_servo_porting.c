/**
  ******************************************************************************
  * @file           : serial_servo.c
  * @brief          : 串行总线舵机控制驱动
  * @author         : liang
  * @date           : 2025-06-24
  * @version        : 1.1
  * - 使用 for 循环逐字节发送数据。
  * - 在收发切换前，严格等待 TC (Transmission Complete) 标志位。
  * - 使用 HAL 中断回调函数处理数据接收。
  * - 为关键循环增加了超时保护，防止程序卡死。
  ******************************************************************************
  */

#include "stm32f1xx_hal.h"
#include "main.h"
#include "serial_servo.h"

extern UART_HandleTypeDef huart2;
SerialServoControllerTypeDef serial_servo_controller;
static uint8_t rx_temp_byte;

/**
 * @brief  通过串行总线发送指令并接收返回数据 
 * @param  self: 控制器实例指针
 * @param  frame: 要发送的数据帧指针
 * @param  tx_only: 是否只发送而不等待接收 (true: 只发不收, false: 发送并接收)
 * @retval 0: 成功, -1: 失败 (超时)
 */
static int serial_write_and_read(SerialServoControllerTypeDef *self, SerialServoCmdTypeDef *frame, bool tx_only) {
    int ret = 0;

    memcpy(&self->tx_frame, frame, sizeof(SerialServoCmdTypeDef));
    uint8_t *tx_buf = (uint8_t*)(&self->tx_frame);
    uint16_t tx_len = self->tx_frame.elements.length + 3; 
    HAL_GPIO_WritePin(BUS_EN_GPIO_Port, BUS_EN_Pin, GPIO_PIN_RESET);
    for (uint16_t i = 0; i < tx_len; i++) {
        uint32_t tick_start_byte = HAL_GetTick();
        while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TXE) == RESET) {
            // 为发送单个字节设置一个短超时，防止卡死
            if (HAL_GetTick() - tick_start_byte > 10) {
                 HAL_GPIO_WritePin(BUS_EN_GPIO_Port, BUS_EN_Pin, GPIO_PIN_SET); 
                 return -1;
            }
        }
        
        huart2.Instance->DR = (tx_buf[i] & (uint8_t)0xFF);
    }

    uint32_t tick_start_tc = HAL_GetTick();
    while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC) == RESET) {
        if (HAL_GetTick() - tick_start_tc > 10) { 
             HAL_GPIO_WritePin(BUS_EN_GPIO_Port, BUS_EN_Pin, GPIO_PIN_SET);
             return -1; // 发送超时错误
        }
    }
    HAL_GPIO_WritePin(BUS_EN_GPIO_Port, BUS_EN_Pin, GPIO_PIN_SET);

    if (tx_only) {
        return 0;
    }
		
    memset(&self->rx_frame, 0, sizeof(SerialServoCmdTypeDef));
    self->rx_state = SERIAL_SERVO_RECV_STARTBYTE_1; 
    self->rx_finished = false;                     

    HAL_UART_Receive_IT(&huart2, &rx_temp_byte, 1);


    uint32_t ticks_recv = HAL_GetTick();
    while (!self->rx_finished) {
        if (HAL_GetTick() - ticks_recv > self->proc_timeout) {
            ret = -1; // 标记为接收超时
            break;
        }
    }

    HAL_UART_AbortReceive_IT(&huart2);
    
    return ret;
}

/**
 * @brief  初始化串行舵机控制器
 * @param  None
 * @retval None
 */
void serial_servo_init(void) {
    serial_servo_controller_object_init(&serial_servo_controller);
    serial_servo_controller.proc_timeout = 100;
    serial_servo_controller.serial_write_and_read = serial_write_and_read;
    HAL_GPIO_WritePin(BUS_EN_GPIO_Port, BUS_EN_Pin, GPIO_PIN_SET);
}

/**
 * @brief  UART接收完成回调函数 (HAL库标准回调函数)
 * @note   当UART每接收到一个字节的数据后，会触发此中断回调
 * @param  huart: 触发中断的UART句柄指针
 * @retval None
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {

        if (serial_servo_rx_handler(&serial_servo_controller, rx_temp_byte) == 0)
        {
            // 如果解析函数返回0，说明已接收到一帧完整且正确的数据
            serial_servo_controller.rx_finished = true;
        }
        else
        {
            // 如果一帧数据尚未接收完整，并且没有超时，则继续使能中断以接收下一个字节
            if (serial_servo_controller.rx_finished == false)
            {
                HAL_UART_Receive_IT(&huart2, &rx_temp_byte, 1);
            }
        }
    }
}
