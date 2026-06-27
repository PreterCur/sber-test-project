/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "main.h"





static const char *INIT_TAG = "INIT";
static const char *DIRECTOR_TAG = "DIRECTOR";
static const char *ERROR_TAG = "ERROR";


static const char version_string[] = "Firmware version: v1.0.2";

static TaskHandle_t button_task_h = NULL;
static TaskHandle_t director_task_h = NULL;
static TaskHandle_t led_task_h = NULL;
static TaskHandle_t measure_task_h = NULL;

static QueueHandle_t xDirectorEvtQueue = NULL;
static QueueHandle_t led_queue_h = NULL;







#define LATCH_SWITCH_PIN        GPIO_NUM_13
#define DEBOUNCE_SAMPLES        10
#define POLLING_PERIOD          5

//Tasks




typedef struct 
{
    
}director_task_config_t;


void director_task(void *pvParameters)
{
    ESP_LOGI(DIRECTOR_TAG, "Director task started\r\n");
    
    system_state_t current_state = STATE_STARTUP;
    generic_event_t evt_queue_recv = {0, };
    
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
                    if (evt_queue_recv.comp_id == COMP_ID_BUTTON)
                    {

                    }
                    else if (evt_queue_recv.comp_id == COMP_ID_MEASURE)
                    {

                    }
                    else if (evt_queue_recv.comp_id == COMP_ID_LED)
                    {
                        //probably empty
                    }
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
    BaseType_t led_ret = xTaskCreatePinnedToCore(led_task_handler, 
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

    BaseType_t btn_ret = xTaskCreatePinnedToCore(button_task_handler, 
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
    BaseType_t measure_ret = xTaskCreatePinnedToCore(measure_task_handler, 
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
