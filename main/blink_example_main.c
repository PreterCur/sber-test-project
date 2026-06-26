/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"

#include "driver/gpio.h"
#include "led_strip.h"
#include "esp_timer.h"

#include "esp_http_client.h"

#include "sdkconfig.h"


static const char *INIT_TAG = "INIT";
static const char *BTN_TAG = "BTN";
static const char *LED_TAG = "LED";
static const char *DIRECTOR_TAG = "DIRECTOR";
static const char *WIFI_TAG = "WIFI";
static const char *ADC_TAG = "ADC";

static const char *ERROR_TAG = "ERROR";


static const char version_string[] = "Firmware version: v1.0.2";

static TaskHandle_t button_task_h = NULL;
static TaskHandle_t director_task_h = NULL;
static TaskHandle_t led_task_h = NULL;
static TaskHandle_t measure_task_h = NULL;

static QueueHandle_t xDirectorEvtQueue = NULL;


/* Use project configuration menu (idf.py menuconfig) to choose the GPIO to blink,
   or you can edit the following line and set a number here.
*/
#define BLINK_GPIO CONFIG_BLINK_GPIO

static uint8_t s_led_state = 0;

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

static QueueHandle_t led_queue_h = NULL;

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

static void configure_led(led_strip_handle_t *led_strip_p)
{
    ESP_LOGI(LED_TAG, "Example configured to blink addressable LED!");
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1, // at least one LED on board
    };
#if CONFIG_BLINK_LED_STRIP_BACKEND_RMT
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, led_strip_p));
#elif CONFIG_BLINK_LED_STRIP_BACKEND_SPI
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, led_strip_p));
#else
#error "unsupported LED strip backend"
#endif
    /* Set all LED off to clear all pixels */
    led_strip_clear(*led_strip_p);
}

static void led_struct_fill(led_str_t *leds_p, uint32_t red, uint32_t green, uint32_t blue, uint32_t blink_period_ms)
{
    leds_p->red = red;
    leds_p->green = green;
    leds_p->blue = blue;
    leds_p->blink_period_ms = blink_period_ms;
}

static void led_write_refresh(led_strip_handle_t *strip_p, led_str_t *leds_p)
{
    int64_t now = esp_timer_get_time();
    int64_t blink_time_elapsed = now - leds_p->last_blink_time;
    if (blink_time_elapsed > leds_p->blink_period_ms * 1000)
    {
        if (false == leds_p->is_led_on)
        {
            led_strip_set_pixel(*strip_p, leds_p->led_num, leds_p->red, leds_p->green, leds_p->blue);
            led_strip_refresh(*strip_p);
            leds_p->is_led_on = true;
        }
        else if (true == leds_p->is_led_on && leds_p->blink_period_ms > 0)
        {
            //do not clear strip if there is no blinks
            led_strip_clear(*strip_p);
            leds_p->is_led_on = false;
        }
        leds_p->last_blink_time = now;
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
}system_state_t;

void led_task(void *pvParameters)
{
    ESP_LOGI(LED_TAG, "LED task started\r\n");

    led_strip_handle_t led_strip;

    led_str_t local_led = {0, };
    int64_t last_blink = 0;
    bool is_led_on = false;

    configure_led(&led_strip);

    uint32_t led_state = 0;
    while (1)
    {
        BaseType_t led_notify_ret = xTaskNotifyWait(0x00, ULONG_MAX, &led_state, pdMS_TO_TICKS(10));
        if (led_notify_ret == pdPASS)
        {
            switch(led_state)
            {
                case(1 << STATE_STARTUP):
                {
                    led_struct_fill(&local_led, 0, 0, 0, 0);
                }
                break;
                case(1 << STATE_INIT):
                {
                    led_struct_fill(&local_led, 0, 20, 0, 200);
                }
                break;
                case(1 << STATE_IDLE):
                {
                    led_struct_fill(&local_led, 0, 20, 0, 0);
                }
                break;
                case(1 << STATE_MEASURING):
                {
                    led_struct_fill(&local_led, 0, 0, 20, 200);
                }
                break;
                case(1 << STATE_WIFI_CONNECTING):
                {
                    led_struct_fill(&local_led, 0, 0, 20, 0);
                }
                break;
                case(1 << STATE_UPLOADING):
                {
                    led_struct_fill(&local_led, 20, 20, 0, 200);
                }
                break;
                case(1 << STATE_OTA_CHECKING):
                case(1 << STATE_OTA_UPDATING):
                {
                    led_struct_fill(&local_led, 20, 20, 0, 0);
                }
                break;
                case(1 << STATE_ERROR):
                {
                    led_struct_fill(&local_led, 20, 0, 0, 200);
                }
                break;
                default:
                {
                    ESP_LOGE(LED_TAG, "Unknown LED state\r\n");
                }
                break;
            }
        }
        led_write_refresh(&led_strip, &local_led);
    }
}



typedef enum 
{
    EVT_BUTTON_SHORT_CLICK,
    EVT_BUTTON_LONG_CLICK,
    EVT_BACKEND_BUFFER_FULL,
    EVT_WIFI_CONNECTED,
    EVT_WIFI_ERROR,
    EVT_UPLOAD_DONE,
    EVT_UPLOAD_ERROR
}event_id_t;

typedef struct 
{
    event_id_t id;
    uint32_t param; // errcode/data_size
}system_event_t;

void director_task(void *pvParameters)
{
    ESP_LOGI(DIRECTOR_TAG, "Director task started\r\n");
    
    system_state_t current_state = STATE_STARTUP;
    system_event_t evt_queue_recv = {0, };
    
    uint32_t notify_val = 0;

    while (1)
    {
        BaseType_t queue_ret = xQueueReceive(xDirectorEvtQueue, &evt_queue_recv, portMAX_DELAY);
        if (queue_ret == pdPASS)
        {
            switch (current_state)
            {
                case (STATE_STARTUP):
                {

                }
                break;
                case (STATE_INIT):
                {

                }
                break;
                case (STATE_IDLE):
                {

                }
                break;
                case (STATE_MEASURING):
                {

                }
                break;
                case (STATE_WIFI_CONNECTING):
                {

                }
                break;
                case (STATE_UPLOADING):
                {

                }
                break;
                case (STATE_OTA_CHECKING):
                {

                }
                break;
                case (STATE_OTA_UPDATING):
                {

                }
                break;
                case (STATE_ERROR):
                {

                }
                break;
                default:
                    break;
            }
        }

        switch (current_state)
        {
            case (STATE_INIT):
            {
                ESP_LOGI(LED_TAG, "State init, green blinking led\r\n");
                xTaskNotify(led_task_h, 1 << STATE_INIT, eSetBits);
            }
            break;
            case (STATE_IDLE):
            {
                ESP_LOGI(LED_TAG, "State IDLE, green led\r\n");
                xTaskNotify(led_task_h, 1 << STATE_IDLE, eSetBits);
            }
            break;
            case (STATE_MEASURING):
            {
                ESP_LOGI(LED_TAG, "State Measuring, blue blinking led\r\n");
                xTaskNotify(led_task_h, 1 << STATE_MEASURING, eSetBits);
            }
            break;
            case (STATE_WIFI_CONNECTING):
            {
                ESP_LOGI(LED_TAG, "State WIFI Connecting, blue led\r\n");
                xTaskNotify(led_task_h, 1 << STATE_WIFI_CONNECTING, eSetBits);
            }
            break;
            case (STATE_UPLOADING):
            {
                ESP_LOGI(LED_TAG, "State Uploading data, yellow blinking led\r\n");
                xTaskNotify(led_task_h, 1 << STATE_UPLOADING, eSetBits);
            }
            break;
            case (STATE_OTA_CHECKING):
            {
                ESP_LOGI(LED_TAG, "State OTA check, yellow led\r\n");
                xTaskNotify(led_task_h, 1 << STATE_OTA_CHECKING, eSetBits);
            }
            break;
            case (STATE_OTA_UPDATING):
            {
                ESP_LOGI(LED_TAG, "State OTA update, yellow led\r\n");
                xTaskNotify(led_task_h, 1 << STATE_OTA_UPDATING, eSetBits);
            }
            break;
            case (STATE_ERROR):
            {
                ESP_LOGE(ERROR_TAG, "ERROR State, RED led\r\n");
                xTaskNotify(led_task_h, 1 << STATE_ERROR, eSetBits);
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

    BaseType_t director_ret = xTaskCreatePinnedToCore(  director_task, 
                                                        "director task", 
                                                        4096, 
                                                        NULL, 
                                                        10, 
                                                        &director_task_h, 
                                                        1);
    if (director_ret == pdPASS)
    {
        ESP_LOGI(INIT_TAG, "director task created\r\n");
    }
    else
    {
        ESP_LOGE(INIT_TAG, "director task creation failed\r\n");
        while (1);
    }
    //LED Task is the main orchestrator task, so high priority
    BaseType_t led_ret = xTaskCreatePinnedToCore(led_task, 
                                                "led task", 
                                                4096, 
                                                NULL, 
                                                1, 
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

    xTaskNotify(director_task_h, 1 << STATE_INIT, eSetBits);

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
    xTaskNotify(director_task_h, 1 << STATE_IDLE, eSetBits);


    return;
}
