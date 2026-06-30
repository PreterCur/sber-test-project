#include <stdio.h>
#include "button.h"


static const char *BTN_TAG = "BTN";

void button_task_handler(void *pvParameters)
{
    ESP_LOGI(BTN_TAG, "Button task started\r\n");

    button_task_config_t *btn_task_cfg_p = (button_task_config_t *)pvParameters;

    button_exti_str *btn_str_p = &btn_task_cfg_p->btn_config_str;

    if (btn_task_cfg_p->evt_queue_p == NULL)
    {
        ESP_LOGE(BTN_TAG, "NULLPTR FOR BTN EVENT QUEUE\r\n");
        while (1);
    }
    QueueHandle_t *evt_queue_p = btn_task_cfg_p->evt_queue_p;

    
    gpio_config_t io_conf = 
    {
        .pin_bit_mask = (1ULL << btn_str_p->btn_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,     // Assumes active-low wiring (Switch to GND)
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE        // STRICTLY DISABLED to prevent ISR storms
    };
    gpio_config(&io_conf);

    
    while (1)
    {
        // ESP_LOGI(BTN_TAG, "btn tick\r\n");
        int pin_level = gpio_get_level(btn_str_p->btn_pin);
        if (pin_level == 0)
        {
            if (btn_str_p->integrator > 0)
            {
                btn_str_p->integrator--;
            }
        }
        else
        {
            if (btn_str_p->integrator < btn_str_p->debounce_samples)
            {
                btn_str_p->integrator++;
            }
        }

        if (btn_str_p->integrator == btn_str_p->debounce_samples && btn_str_p->debounced_pin_state == 0)
        {
            btn_str_p->debounced_pin_state = 1;
            btn_str_p->push_timestamp_us = esp_timer_get_time();
            // ESP_LOGI(BTN_TAG, "debounced button pushed, time = %lld\r\n", btn_str.push_timestamp_us);
        }
        else if (btn_str_p->integrator == 0 && btn_str_p->debounced_pin_state != 0)
        {
            btn_str_p->debounced_pin_state = 0;
            btn_str_p->release_timestamp_us = esp_timer_get_time();
            // ESP_LOGI(BTN_TAG, "debounced button released, time = %lld\r\n", btn_str.release_timestamp_us);
            int64_t press_length_us = btn_str_p->release_timestamp_us - btn_str_p->push_timestamp_us;

            if (press_length_us > btn_str_p->short_press_period_ms * 1000 && press_length_us < btn_str_p->long_press_period_ms * 1000)
            {
                ESP_LOGI(BTN_TAG, "short press detected, length = %lld\r\n", press_length_us / 1000);

                generic_event_t short_btn_evt = 
                {
                    .comp_id    = COMP_ID_BUTTON,
                    .event_id   = EVT_BUTTON_SHORT_CLICK,
                    .param      = 0
                };
                //wait for 10 ticks if director is not available
                BaseType_t btn_q_ret = xQueueSend(*evt_queue_p, &short_btn_evt, 10);
                if (btn_q_ret != pdTRUE)
                {
                    ESP_LOGE(BTN_TAG, "Failed to send btn short press event to evt queue!\r\n");
                }
            }
            else if (press_length_us >= btn_str_p->long_press_period_ms * 1000)
            {
                ESP_LOGI(BTN_TAG, "long press detected, length = %lld\r\n", press_length_us / 1000);
                generic_event_t long_btn_evt = 
                {
                    .comp_id    = COMP_ID_BUTTON,
                    .event_id   = EVT_BUTTON_LONG_CLICK,
                    .param      = 0
                };
                //wait for 10 ticks if director is not available
                BaseType_t btn_q_ret = xQueueSend(*evt_queue_p, &long_btn_evt, 10);
                if (btn_q_ret != pdTRUE)
                {
                    ESP_LOGE(BTN_TAG, "Failed to send btn long press event to evt queue!\r\n");
                }
            }
        }
        //freertos has 100Hz tickrate, polling period less than 10ms crashes the program
        vTaskDelay(pdMS_TO_TICKS(btn_str_p->polling_period_ms));
    }
}
