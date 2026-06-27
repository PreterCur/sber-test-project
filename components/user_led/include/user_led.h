#ifndef USER_LED_H
#define USER_LED_H

#include "esp_log.h"

#include "led_strip.h"

#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "system_events.h"


typedef enum 
{
    LED_CMD_OFF,
    LED_CMD_SOLID_GREEN,
    LED_CMD_BLINK_GREEN,
    LED_CMD_SOLID_BLUE,
    LED_CMD_BLINK_BLUE,
    LED_CMD_SOLID_YELLOW,
    LED_CMD_BLINK_YELLOW,
    LED_CMD_BLINK_RED
} led_command_t;


typedef struct
{
    uint32_t    red;
    uint32_t    green;
    uint32_t    blue;

    uint32_t    led_num;

    uint32_t    blink_period_ms;
    int64_t     last_blink_time;

    bool        is_led_on;
}led_str_t;

void led_task_handler(void *pvParameters);

#endif