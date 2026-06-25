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
#include "led_strip.h"

#include "esp_timer.h"

#include "sdkconfig.h"


static const char *INIT_TAG = "INIT";
static const char *BTN_TAG = "BTN";
static const char *LED_TAG = "LED";
static const char *WIFI_TAG = "WIFI";
static const char *ADC_TAG = "ADC";

static const char *ERROR_TAG = "ERROR";


static const char version_string[] = "Firmware version: v1.0.2";

static TaskHandle_t button_task_h = NULL;
static TaskHandle_t led_task_h = NULL;
static TaskHandle_t measure_task_h = NULL;


/* Use project configuration menu (idf.py menuconfig) to choose the GPIO to blink,
   or you can edit the following line and set a number here.
*/
#define BLINK_GPIO CONFIG_BLINK_GPIO

static uint8_t s_led_state = 0;


static void blink_led(void)
{
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    gpio_set_level(BLINK_GPIO, s_led_state);
}

static void configure_led(void)
{
    ESP_LOGI(INIT_TAG, "Example configured to blink GPIO LED!");
    gpio_reset_pin(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}


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

#define LATCH_SWITCH_PIN        GPIO_NUM_13
#define DEBOUNCE_SAMPLES        10
#define POLLING_PERIOD          5
//Tasks

void button_task(void *pvParameters)
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

typedef enum
{
    STATE_STARTUP = 0, 
    STATE_INIT,
    STATE_IDLE,
    STATE_MEASURING,
    STATE_WIFI_CONNECTING,
    STATE_UPLOADING,
    STATE_OTA_CHECKING,
    STATE_OTA_UPDATING,
    STATE_ERROR
}LED_STATE_t;

typedef enum
{
    STATE_CMD_STARTUP_TO_INIT = (uint32_t)(1 << 0),
    STATE_CMD_INIT_TO_IDLE = (uint32_t)(1 << 1),
    STATE_CMD_MEASURE = (uint32_t)(1 << 2),
    STATE_CMD_MEASURE_TO_WIFI_CONNECTING = (uint32_t)(1 << 3),
    STATE_CMD_WIFI_CONNECT_TO_UPLOADING = (uint32_t)(1 << 4),
    STATE_CMD_OTA_START = (uint32_t)(1 << 5),
    STATE_CMD_ERROR_RESET = (uint32_t)(1 << 31),
}state_task_notify_cmd_t;

typedef enum
{
    MEASURE_CMD_START = (1 << 10),
    MEASURE_CMD_STOP = (1 << 11),
    MEASURE_CMD_WIFI_CONNECT_CALLBACK = (1 << 12),
    MEASURE_CMD_WIFI_UPLOAD_CALLBACK = (1 << 13),
}measure_task_cmd_t;

void led_task(void *pvParameters)
{
    ESP_LOGI(LED_TAG, "LED task started\r\n");

    configure_led();

    LED_STATE_t current_state = STATE_STARTUP;
    uint32_t notify_val = 0;
    while (1)
    {
        BaseType_t notify_ret = xTaskNotifyWait(0x00, ULONG_MAX, &notify_val, portMAX_DELAY);
        if (notify_ret == pdPASS)
        {
            if (notify_val & STATE_CMD_STARTUP_TO_INIT && current_state == STATE_STARTUP)
            {
                ESP_LOGI(LED_TAG, "startup to init cmd\r\n");
                current_state = STATE_INIT;
            }
            else if (notify_val & STATE_CMD_INIT_TO_IDLE && current_state == STATE_INIT)
            {
                ESP_LOGI(LED_TAG, "init to idle cmd\r\n");
                current_state = STATE_IDLE;
            }
            else if (notify_val & STATE_CMD_MEASURE)
            {
                ESP_LOGI(LED_TAG, "measure cmd\r\n");
                if (NULL == measure_task_h)
                {
                    ESP_LOGE(LED_TAG, "measure task not initialized");
                }
                else
                {
                    if (current_state == STATE_IDLE)
                    {
                        xTaskNotify(measure_task_h, MEASURE_CMD_START, eSetBits);
                    }
                    else if (current_state == STATE_MEASURING)
                    {
                        xTaskNotify(measure_task_h, MEASURE_CMD_STOP, eSetBits);
                    }
                }
            }
            else if (notify_val & STATE_CMD_MEASURE_TO_WIFI_CONNECTING)
            {
                //notification from measure task that it started wifi connection
                current_state = STATE_WIFI_CONNECTING;
                //WARNING might need to set LEDS here and give notification back to wifi process for having correct indication at the right time
                xTaskNotify(measure_task_h, MEASURE_CMD_WIFI_CONNECT_CALLBACK, eSetBits);
            }
            else if (notify_val & STATE_CMD_WIFI_CONNECT_TO_UPLOADING)
            {
                //notification from measure task that it started data upload
                current_state = STATE_UPLOADING;
                xTaskNotify(measure_task_h, MEASURE_CMD_WIFI_UPLOAD_CALLBACK, eSetBits);
            }
            else if (notify_val & STATE_CMD_OTA_START)
            {

            }
            else if (notify_val & STATE_CMD_ERROR_RESET)
            {

            }
        }

        switch (current_state)
        {
            case (STATE_INIT):
            {
                ESP_LOGI(LED_TAG, "State init, green blinking led\r\n");
                gpio_set_level(BLINK_GPIO, 0);
            }
            break;
            case (STATE_IDLE):
            {
                ESP_LOGI(LED_TAG, "State IDLE, green led\r\n");
                gpio_set_level(BLINK_GPIO, 1);
            }
            break;
            case (STATE_MEASURING):
            {
                ESP_LOGI(LED_TAG, "State Measuring, blue blinking led\r\n");
            }
            break;
            case (STATE_WIFI_CONNECTING):
            {
                ESP_LOGI(LED_TAG, "State WIFI Connecting, blue led\r\n");
            }
            break;
            case (STATE_UPLOADING):
            {
                ESP_LOGI(LED_TAG, "State Uploading data, yellow blinking led\r\n");
            }
            break;
            case (STATE_OTA_CHECKING):
            case (STATE_OTA_UPDATING):
            {
                ESP_LOGI(LED_TAG, "State OTA check/update, yellow led\r\n");
            }
            break;
            case (STATE_ERROR):
            {
                ESP_LOGE(ERROR_TAG, "ERROR State, RED led\r\n");
                if (notify_val & STATE_CMD_ERROR_RESET)
                {
                    current_state = STATE_IDLE;
                }
            }
            break;
            default:
                break;
        }
    }
}

void measure_task(void *pvParameters)
{
    ESP_LOGI(LED_TAG, "Measure task started\r\n");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    ESP_LOGI(INIT_TAG, "%s\r\n", version_string);
    /* Configure the peripheral according to the LED type */

    //LED Task is the main orchestrator task, so high priority
    BaseType_t led_ret = xTaskCreatePinnedToCore(led_task, 
                                                "led task", 
                                                4096, 
                                                NULL, 
                                                10, 
                                                &led_task_h, 
                                                1);
    if (led_ret == pdPASS)
    {
        ESP_LOGI(INIT_TAG, "led task created\r\n");
    }
    else
    {
        ESP_LOGE(INIT_TAG, "led task creation failed\r\n");
        while (1);
    }

    xTaskNotify(led_task_h, STATE_INIT, eSetBits);

    BaseType_t btn_ret = xTaskCreatePinnedToCore(button_task, 
                                                "Btn task", 
                                                4096, 
                                                NULL, 
                                                7, 
                                                &button_task_h, 
                                                1);
    if (btn_ret == pdPASS)
    {
        ESP_LOGI(INIT_TAG, "btn task created\r\n");
    }
    else
    {
        ESP_LOGE(INIT_TAG, "btn task creation failed\r\n");
        while (1);
    }

    //wifi task should be pinned to core 0 but am not sure
    BaseType_t measure_ret = xTaskCreatePinnedToCore(measure_task, 
                                                    "Btn task", 
                                                    4096, 
                                                    NULL, 
                                                    5, 
                                                    &measure_task_h, 
                                                    0);
    
    if (measure_ret == pdPASS)
    {
        ESP_LOGI(INIT_TAG, "measure task created\r\n");
    }
    else
    {
        ESP_LOGE(INIT_TAG, "measure task creation failed\r\n");
        while (1);
    }

    //INIT DONE, MOVE ON TO IDLE STATE
    xTaskNotify(led_task_h, STATE_IDLE, eSetBits);


    return;
}
