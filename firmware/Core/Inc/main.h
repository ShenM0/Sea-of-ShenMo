/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "buzzer.h"
#include "led.h"
#include "serial_servo.h" 
#include "packet.h"

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define ADC_BAT_Pin GPIO_PIN_3
#define ADC_BAT_GPIO_Port GPIOC
#define BUS_EN_Pin GPIO_PIN_1
#define BUS_EN_GPIO_Port GPIOA
#define BUS_TX_Pin GPIO_PIN_2
#define BUS_TX_GPIO_Port GPIOA
#define BUS_RX_Pin GPIO_PIN_3
#define BUS_RX_GPIO_Port GPIOA
#define LED1_Pin GPIO_PIN_15
#define LED1_GPIO_Port GPIOB
#define IO18_Pin GPIO_PIN_4
#define IO18_GPIO_Port GPIOB
#define IO19_Pin GPIO_PIN_5
#define IO19_GPIO_Port GPIOB
#define BUZZER_Pin GPIO_PIN_8
#define BUZZER_GPIO_Port GPIOB
#define LED_Pin GPIO_PIN_9
#define LED_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
extern __IO uint32_t sys_count;
static inline uint32_t get_ticks(void) {
  return sys_count;
}
extern void leds_init(void);
extern LEDObjectTypeDef leds[2];
void buzzers_init(void);

extern BuzzerObjectTypeDef buzzers[1]; 

extern void packet_init(void);
extern void start_recv(void);
extern void recv_task(void);
void leds_init(void);


void leds_task_poll(void);

extern void packet_handle_init(void);


extern struct PacketController packet_controller; 
extern void serial_servo_init(void);

void serial_servo_init_porting(void);


/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
