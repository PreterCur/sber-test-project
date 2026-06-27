#include <stdio.h>
#include "button.h"


static const char *BTN_TAG = "BTN";

void button_task_handler(void *pvParameters)
{
    ESP_LOGI(BTN_TAG, "Button task started\r\n");

    button_exti_str btn_str = 
    {
        .btn_pin = GPIO_NUM_13,
        .integrator = 0,
        .long_press_period_ms = 3000,
        .short_press_period_ms = 200,
        .debounce_samples = 10,
        .polling_period_ms = 10,
    };

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << btn_str.btn_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,     // Assumes active-low wiring (Switch to GND)
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE        // STRICTLY DISABLED to prevent ISR storms
    };
    gpio_config(&io_conf);

    while (1)
    {
        // ESP_LOGI(BTN_TAG, "btn tick\r\n");
        int pin_level = gpio_get_level(btn_str.btn_pin);
        if (pin_level == 0)
        {
            if (btn_str.integrator > 0)
            {
                btn_str.integrator--;
            }
        }
        else
        {
            if (btn_str.integrator < btn_str.debounce_samples)
            {
                btn_str.integrator++;
            }
        }

        if (btn_str.integrator == btn_str.debounce_samples && btn_str.debounced_pin_state == 0)
        {
            btn_str.debounced_pin_state = 1;
            btn_str.push_timestamp_us = esp_timer_get_time();
            // ESP_LOGI(BTN_TAG, "debounced button pushed, time = %lld\r\n", btn_str.push_timestamp_us);
        }
        else if (btn_str.integrator == 0 && btn_str.debounced_pin_state != 0)
        {
            btn_str.debounced_pin_state = 0;
            btn_str.release_timestamp_us = esp_timer_get_time();
            // ESP_LOGI(BTN_TAG, "debounced button released, time = %lld\r\n", btn_str.release_timestamp_us);
            int64_t press_length_us = btn_str.release_timestamp_us - btn_str.push_timestamp_us;

            if (press_length_us > btn_str.short_press_period_ms * 1000 && press_length_us < btn_str.long_press_period_ms * 1000)
            {
                ESP_LOGI(BTN_TAG, "short press detected, length = %lld\r\n", press_length_us / 1000);

            }
            else if (press_length_us >= btn_str.long_press_period_ms * 1000)
            {
                ESP_LOGI(BTN_TAG, "long press detected, length = %lld\r\n", press_length_us / 1000);
            }
        }

        //freertos has 100Hz tickrate, polling period less than 10ms crashes the program
        vTaskDelay(pdMS_TO_TICKS(btn_str.polling_period_ms));
        // vTaskDelay(1);
    }
}
