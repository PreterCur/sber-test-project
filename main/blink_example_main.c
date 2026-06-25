/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "esp_timer.h"


static const char *INIT_TAG = "INIT";
static const char *BTN_TAG = "BTN";
static const char *WIFI_TAG = "WIFI";
static const char *ADC_TAG = "ADC";

static const char version_string[] = "Firmware version: v1.0.0";

static TaskHandle_t button_task_h = NULL;

typedef struct
{
    TaskHandle_t *btn_task;
    uint32_t exti_pin;
    uint32_t pin_logic_level;
    uint64_t debounce_period;
    uint64_t last_isr_timestamp;
    uint64_t press_start_timestamp;
    uint64_t press_end_timestamp;
    uint64_t long_press_period;
    uint64_t short_press_period;
    uint64_t last_press_length;
}button_exti_str;

#define BTN_NOTIFY_LONG_PRESS   (uint32_t)1
#define BTN_NOTIFY_SHORT_PRESS  (uint32_t)2

// ISR handler function
static void IRAM_ATTR gpio_isr_handler(void* arg) 
{
    if (arg == NULL)
    {
        ESP_LOGE(BTN_TAG, "NULL arg to gpio isr handler\r\n");
        return;
    }

    button_exti_str *btn_p = (button_exti_str *)arg;
    uint64_t now = esp_timer_get_time() / 1000;
    if (now - btn_p->last_isr_timestamp > btn_p->debounce_period)
    {
        btn_p->last_isr_timestamp = now;
        int level = gpio_get_level(btn_p->exti_pin);
        if (level != btn_p->pin_logic_level)
        {
            //button pushed
            btn_p->press_start_timestamp = now;
        }
        else
        {
            //button released
            btn_p->press_end_timestamp = now;
            uint64_t press_length = btn_p->press_start_timestamp - btn_p->press_end_timestamp;
            btn_p->last_press_length = press_length;
            if (press_length > btn_p->short_press_period &&
                press_length < btn_p->long_press_period)
            {
                BaseType_t higher_priority_task_woken = pdFALSE;
                xTaskNotifyFromISR(*btn_p->btn_task, 
                                    BTN_NOTIFY_SHORT_PRESS, 
                                    eSetBits, 
                                    &higher_priority_task_woken);
                btn_p->last_isr_timestamp = now;
                if (higher_priority_task_woken) 
                {
                    portYIELD_FROM_ISR();
                }
            }
            else if (press_length >= btn_p->long_press_period)
            {
                BaseType_t higher_priority_task_woken = pdFALSE;
                xTaskNotifyFromISR(*btn_p->btn_task, 
                                    BTN_NOTIFY_LONG_PRESS, 
                                    eSetBits, 
                                    &higher_priority_task_woken);
                btn_p->last_isr_timestamp = now;
                if (higher_priority_task_woken) 
                {
                    portYIELD_FROM_ISR();
                }
            }
        }
    }
    // Notify task or set flag here
}



void button_task(void *pvParameters)
{
    ESP_LOGI(BTN_TAG, "Button task started\r\n");

    button_exti_str btn_str = 
    {
        .btn_task = &button_task_h,
        .exti_pin = GPIO_NUM_5,
        .debounce_period = 50,
        .press_start_timestamp = 0,
        .press_end_timestamp = 0,
        .long_press_period = 5000,
        .short_press_period = 200,
        .pin_logic_level = 0,
    };

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,         // Interrupt on falling edge
        .mode = GPIO_MODE_INPUT,                // Set as input
        .pin_bit_mask = (1ULL << btn_str.exti_pin),// Bit mask for the pin
        .pull_up_en = GPIO_PULLUP_ENABLE,       // Enable Internal Pull-Up (IPU)
        .pull_down_en = GPIO_PULLDOWN_DISABLE   // Disable Pull-Down
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    ESP_ERROR_CHECK(gpio_isr_handler_add(btn_str.exti_pin, gpio_isr_handler, (void *)&btn_str));

    uint32_t notify_val = 0x00;
    while (1)
    {
        ESP_LOGI(BTN_TAG, "btn task tick");
        BaseType_t btn_notify_ret = xTaskNotifyWait(0x00, ULONG_MAX, &notify_val, pdMS_TO_TICKS(1000));
        if (btn_notify_ret == pdPASS)
        {
            if (notify_val & BTN_NOTIFY_LONG_PRESS)
            {
                ESP_LOGI(BTN_TAG, "Short press detected, length = %lu\r\n", btn_str.last_press_length);
            }
            else if (notify_val & BTN_NOTIFY_SHORT_PRESS)
            {
                ESP_LOGI(BTN_TAG, "Long press detected, length = %lu\r\n", btn_str.last_press_length);
            }
            else
            {
                ESP_LOGW(BTN_TAG, "Unknown notify from btn ISR");
            }
        }
    }
}






void app_main(void)
{
    ESP_LOGI(INIT_TAG, "%s\r\n", version_string);

    BaseType_t btn_ret = xTaskCreatePinnedToCore(button_task, 
                                                "Btn task", 
                                                4096, 
                                                NULL, 
                                                5, 
                                                &button_task_h, 
                                                1);

    if (btn_ret == pdPASS)
    {
        ESP_LOGI(INIT_TAG, "btn task created\r\n");
    }
    else
    {
        ESP_LOGE(INIT_TAG, "btn task creation failed\r\n");
    }

    return;
}
