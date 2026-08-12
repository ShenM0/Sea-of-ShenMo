/* battery_handle.c */

#include "battery_handle.h"
#include "adc.h"
#include "packet.h"
#include "packet_reports.h"
#include "buzzer.h"

extern struct PacketController packet_controller;
extern BuzzerObjectTypeDef buzzers[1];
extern ADC_HandleTypeDef hadc1;


#define VOLTAGE_DIVIDER_RATIO       11.0f   // ???????????? (R1+R2)/R2
#define VREFINT_VOLTAGE_MV          1200.0f 
#define BATTERY_TASK_PERIOD         10      
#define REPORT_PERIOD_MS            100     // ??????????10ms
#define ALARM_TRIGGER_SECONDS       10      

/* ????????? */
static uint16_t adc_dma_buffer[2];     
static float    battery_volt_mv = 0.0f;     
static uint16_t battery_min_limit = 6400;   // ??????????, ??¦ËmV

/**
 * @brief ?????????????? (???????)
 */
static void trigger_buzzer_alarm(void)
{
    buzzers[0].beep(&buzzers[0], 2100, 800, 200, 5);
}

/**
 * @brief ???????????? 
 */
static void send_battery_report(void)
{
    uint16_t voltage_to_report = (uint16_t)(battery_volt_mv + 0.5f);

    PacketReportBatteryVoltageTypeDef report = {
        .sub_cmd = 0x04,
        .voltage = voltage_to_report,
    };
    packet_controller.transmit(&packet_controller, PACKET_FUNC_SYS, (uint8_t *)&report, sizeof(report));
}


void battery_handle_init(void)
{
    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_dma_buffer, 2) != HAL_OK)
    {
        Error_Handler();
    }
}

void battery_check(void)
{
    static uint32_t check_ticks = 0;
    static uint32_t report_ticks = 0;
    static int count = 0;  
    uint32_t current_ticks;

    current_ticks = HAL_GetTick(); 

    if (current_ticks - check_ticks >= BATTERY_TASK_PERIOD)
    {
        check_ticks = current_ticks;

        uint16_t adc_bat_raw = adc_dma_buffer[0];  
        uint16_t adc_vref_raw = adc_dma_buffer[1]; 

        if (adc_vref_raw != 0 && adc_vref_raw != 4095)
        {
            float current_volt = ((float)adc_bat_raw / (float)adc_vref_raw) * VREFINT_VOLTAGE_MV * VOLTAGE_DIVIDER_RATIO;

            if (current_volt > 20000.0f) { 
                current_volt = battery_volt_mv; 
            }

            if (battery_volt_mv == 0.0f) { 
                battery_volt_mv = current_volt;
            } else { 
                battery_volt_mv = battery_volt_mv * 0.99f + current_volt * 0.01f;
            }
        }
        if (battery_volt_mv < battery_min_limit && battery_volt_mv > 4900.0f) {
            count++;
        } else {
            count = 0;
        }


        if (count >= (int)(ALARM_TRIGGER_SECONDS * 1000 / BATTERY_TASK_PERIOD))
        {
            trigger_buzzer_alarm();
            count = 0; 
        }
    }
    
    if (current_ticks - report_ticks >= REPORT_PERIOD_MS)
    {
        report_ticks = current_ticks;
        send_battery_report();
    }
}

void change_battery_limit(uint16_t limit)
{
    battery_min_limit = limit;
}
