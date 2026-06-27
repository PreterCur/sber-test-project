#ifndef BUTTON_H
#define BUTTON_H

#include "driver/gpio.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "system_events.h"

typedef enum
{
    EVT_BUTTON_SHORT_CLICK = 0,
    EVT_BUTTON_LONG_CLICK,
}btn_evt_t;

typedef struct
{
    gpio_num_t btn_pin;
    uint32_t pin_logic_level;
    uint32_t debounce_samples;
    uint32_t polling_period_ms;

    uint32_t integrator;
    uint32_t debounced_pin_state;

    int64_t long_press_period_ms;
    int64_t short_press_period_ms;

    int64_t push_timestamp_us;
    int64_t release_timestamp_us;

}button_exti_str;

void button_task_handler(void *pvParameters);


#endif