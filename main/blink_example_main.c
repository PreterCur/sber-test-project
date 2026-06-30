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

esp_err_t state_update_led(TaskHandle_t led_task, system_state_t new_state)
{
    int state_color = -1;
    switch (new_state)
    {
        case(STATE_INIT):
        {
            state_color = LED_CMD_BLINK_GREEN;
        }
        break;
        case(STATE_IDLE):
        {
            state_color = LED_CMD_SOLID_GREEN;
        }
        break;
        case(STATE_MEASURING):
        {
            state_color = LED_CMD_BLINK_BLUE;
        }
        break;
        case(STATE_WIFI_CONNECTING):
        {
            state_color = LED_CMD_SOLID_BLUE;
        }
        break;
        case(STATE_UPLOADING):
        {
            state_color = LED_CMD_BLINK_YELLOW;
        }
        break;
        case(STATE_OTA_CHECKING):
        case(STATE_OTA_UPDATING):
        {
            state_color = LED_CMD_SOLID_YELLOW;
        }
        break;
        case(STATE_ERROR):
        {
            state_color = LED_CMD_BLINK_RED;
        }
        break;
        default:
        {
            ESP_LOGE(DIRECTOR_TAG, "Unknown system state for LEDS");
            return ESP_FAIL;
        }
        break;
    }
    xTaskNotify(led_task, BIT(state_color), eSetBits);
    return ESP_OK;
}



void director_task(void *pvParameters)
{
    ESP_LOGI(DIRECTOR_TAG, "Director task started\r\n");
    
    director_task_config_t *task_cfg_p = (director_task_config_t *)pvParameters;

    system_state_t current_state = STATE_INIT;
    generic_event_t evt_queue_recv = {0, };
    
    uint32_t notify_val = 0;

    while (1)
    {
        BaseType_t queue_ret = xQueueReceive(*task_cfg_p->evt_queue, &evt_queue_recv, portMAX_DELAY);
        if (queue_ret == pdPASS)
        {
            switch (current_state)
            {
                case (STATE_INIT):
                {
                    if (evt_queue_recv.comp_id == COMP_ID_MAIN)
                    {
                        if (evt_queue_recv.event_id == INIT_STARTED)
                        {
                            ESP_LOGI(DIRECTOR_TAG, "Started init\r\n");
                            state_update_led(*task_cfg_p->led_task, current_state);
                        }
                        else if (evt_queue_recv.event_id == INIT_ENDED)
                        {
                            ESP_LOGI(DIRECTOR_TAG, "Finished init, go to idle state\r\n");
                            current_state = STATE_IDLE;
                            state_update_led(*task_cfg_p->led_task, current_state);
                        }
                        else
                        {
                            ESP_LOGE(DIRECTOR_TAG, "Unknown MAIN Event ID, id = %d\r\n", evt_queue_recv.event_id);
                        }
                    }
                    else
                    {
                        ESP_LOGE(DIRECTOR_TAG, "INIT state unsupported command\r\n");
                    }
                }
                break;
                case (STATE_IDLE):
                {
                    if (evt_queue_recv.comp_id == COMP_ID_BUTTON)
                    {
                        switch(evt_queue_recv.event_id)
                        {
                            case(EVT_BUTTON_SHORT_CLICK):
                            {
                                ESP_LOGI(DIRECTOR_TAG, "Send START cmd to measure task\r\n");
                                current_state = STATE_MEASURING;
                                state_update_led(*task_cfg_p->led_task, current_state);
                                xTaskNotify(*task_cfg_p->measure_task, BIT(MEASURE_START), eSetBits);
                            }
                            break;
                            case(EVT_BUTTON_LONG_CLICK):
                            {
                                ESP_LOGI(DIRECTOR_TAG, "Long click detected, Starting OTA Sequence");
                                current_state = STATE_OTA_CHECKING;
                                state_update_led(*task_cfg_p->led_task, current_state);

                                //WIFI was started by measure task
                                // esp_err_t wifi_init_ret = wifi_station_init(EXAMPLE_ESP_WIFI_SSID, sizeof(EXAMPLE_ESP_WIFI_SSID), EXAMPLE_ESP_WIFI_PASS, sizeof(EXAMPLE_ESP_WIFI_PASS));
                                // ESP_ERROR_CHECK(wifi_init_ret);
                                esp_err_t wifi_connect_ret = wifi_config_connect();
                                ESP_ERROR_CHECK(wifi_connect_ret);

                                char available_version[32] = {0, };
                                esp_err_t get_vers_ret = get_available_version(available_version, sizeof(available_version));
                                if (get_vers_ret != ESP_OK)
                                {
                                    ESP_LOGE(DIRECTOR_TAG, "OTA Version Check failed");
                                    generic_event_t get_version_failed_evt = 
                                    {
                                        .comp_id = COMP_ID_MAIN,
                                        .event_id = EVT_OTA_GET_VERS_ERROR,
                                        .param = 0
                                    };
                                    BaseType_t get_vers_q_ret = xQueueSend(*task_cfg_p->evt_queue, &get_version_failed_evt, 0);
                                    if (get_vers_q_ret != pdTRUE)
                                    {
                                        ESP_LOGE(DIRECTOR_TAG, "Failed to write to evt queue from OTA Get Version\r\n");
                                        break;
                                    }
                                    break;
                                }

                                ESP_LOGI(DIRECTOR_TAG, "Available firmware version: %s", available_version);
                                const esp_app_desc_t *app_desc = esp_app_get_description();
                                if (strcmp(app_desc->version, available_version) == 0) 
                                {
                                    ESP_LOGI(DIRECTOR_TAG, "Firmware is already up to date.");
                                    generic_event_t version_recent_evt = 
                                    {
                                        .comp_id = COMP_ID_MAIN,
                                        .event_id = EVT_OTA_FW_UP_TO_DATE,
                                        .param = 0
                                    };
                                    BaseType_t recent_vers_q_ret = xQueueSend(*task_cfg_p->evt_queue, &version_recent_evt, 0);
                                    if (recent_vers_q_ret != pdTRUE)
                                    {
                                        ESP_LOGE(DIRECTOR_TAG, "Failed to write to evt queue from OTA Get Version\r\n");
                                        break;
                                    }

                                    current_state = STATE_IDLE;
                                    state_update_led(*task_cfg_p->led_task, current_state);
                                    break;
                                }
                                
                                generic_event_t ota_vers_update_ready_evt = 
                                {
                                    .comp_id = COMP_ID_MAIN,
                                    .event_id = EVT_OTA_VERSION_UPDATE_READY,
                                    .param = 0
                                };
                                BaseType_t vers_update_q_ret = xQueueSend(*task_cfg_p->evt_queue, &ota_vers_update_ready_evt, 0);
                                if (vers_update_q_ret != pdTRUE)
                                {
                                    ESP_LOGE(DIRECTOR_TAG, "Failed to write to evt queue from BUF_FULL\r\n");
                                    break;
                                }

                            }
                            break;
                            default:
                            {
                                ESP_LOGE(DIRECTOR_TAG, "Unsupported BUTTON cmd!\r\n");
                            }
                            break;
                        }
                    }
                    else
                    {
                        //probably empty
                        ESP_LOGE(DIRECTOR_TAG, "Unsupported event for IDLE state\r\n");
                    }
                }
                break;
                case (STATE_MEASURING):
                {
                    if (evt_queue_recv.comp_id == COMP_ID_BUTTON)
                    {
                        switch(evt_queue_recv.event_id)
                        {
                            case(EVT_BUTTON_SHORT_CLICK):
                            {
                                ESP_LOGI(DIRECTOR_TAG, "Send BTN interrupt cmd to measure task\r\n");
                                xTaskNotify(*task_cfg_p->measure_task, BIT(MEASURE_BTN_INTERRUPT), eSetBits);
                            }
                            break;
                            default:
                            {
                                ESP_LOGE(DIRECTOR_TAG, "Unsupported BUTTON cmd!\r\n");
                            }
                            break;
                        }
                    }
                    else if (evt_queue_recv.comp_id == COMP_ID_MEASURE)
                    {
                        switch(evt_queue_recv.event_id)
                        {
                            case(EVT_ADC_BTN_INTERRUPT):
                            {
                                ESP_LOGI(DIRECTOR_TAG, "ADC stop event from BTN IRQ, ready to send data\r\n");
                                xTaskNotify(*task_cfg_p->measure_task, BIT(MEASURE_GENERATE_CSV), eSetBits);
                            }
                            break;
                            case(EVT_ADC_FULL_INTERRUPT):
                            {
                                ESP_LOGI(DIRECTOR_TAG, "ADC stop event, buf full IRQ, ready to send data\r\n");
                                xTaskNotify(*task_cfg_p->measure_task, BIT(MEASURE_GENERATE_CSV), eSetBits);
                            }
                            break;
                            case(EVT_ADC_OVERFLOW_ERR):
                            {
                                ESP_LOGE(DIRECTOR_TAG, "ADC Overflow ERR event!\r\n");
                                current_state = STATE_ERROR;
                                state_update_led(*task_cfg_p->led_task, current_state);
                            }
                            break;
                            case(EVT_CSV_CREATED):
                            {
                                ESP_LOGI(DIRECTOR_TAG, "CSV Created event, start wifi sequence\r\n");
                                current_state = STATE_WIFI_CONNECTING;
                                state_update_led(*task_cfg_p->led_task, current_state);
                                xTaskNotify(*task_cfg_p->measure_task, BIT(MEASURE_WIFI_CONNECT), eSetBits);
                            }
                            break;
                            case(EVT_CSV_CREATE_ERROR):
                            {
                                ESP_LOGE(DIRECTOR_TAG, "CSV Creation Error\r\n");
                                current_state = STATE_ERROR;
                                state_update_led(*task_cfg_p->led_task, current_state);
                            }
                            break;
                            default:
                            {
                                ESP_LOGE(DIRECTOR_TAG, "Unsupported STATE_MEASURING event!\r\n");
                            }
                            break;
                        }
                    }
                }
                break;
                case (STATE_WIFI_CONNECTING):
                {
                    if (evt_queue_recv.comp_id == COMP_ID_MEASURE)
                    {
                        switch(evt_queue_recv.event_id)
                        {
                            case(EVT_WIFI_CONNECTED):
                            {
                                ESP_LOGI(DIRECTOR_TAG, "WIFI Connected received in main, Start uploading");
                                current_state = STATE_UPLOADING;
                                state_update_led(*task_cfg_p->led_task, current_state);
                                xTaskNotify(*task_cfg_p->measure_task, BIT(MEASURE_START_UPLOAD), eSetBits);
                            }
                            break;
                            case(EVT_WIFI_ERROR):
                            {
                                ESP_LOGE(DIRECTOR_TAG, "WIFI ERR received in main");
                                current_state = STATE_ERROR;
                                state_update_led(*task_cfg_p->led_task, current_state);
                            }
                            break;
                            default:
                            {
                                ESP_LOGE(DIRECTOR_TAG, "Unsupported STATE_WIFI_CONNECTING event!\r\n");
                            }
                            break;
                        }
                    }
                }
                break;
                case (STATE_UPLOADING):
                {
                    if (evt_queue_recv.comp_id == COMP_ID_MEASURE)
                    {
                        switch(evt_queue_recv.event_id)
                        {
                            case(EVT_UPLOAD_DONE):
                            {
                                ESP_LOGI(DIRECTOR_TAG, "Upload SUCCESS received in main, back to idle");
                                current_state = STATE_IDLE;
                                state_update_led(*task_cfg_p->led_task, current_state);
                            }
                            break;
                            case(EVT_UPLOAD_ERROR):
                            {
                                ESP_LOGE(DIRECTOR_TAG, "Upload ERR received in main");
                                current_state = STATE_ERROR;
                                state_update_led(*task_cfg_p->led_task, current_state);
                            }
                            break;
                            default:
                            {
                                ESP_LOGE(DIRECTOR_TAG, "Unsupported STATE_UPLOADING event!\r\n");
                            }
                            break;
                        }
                    }
                }
                break;
                case (STATE_OTA_CHECKING):
                {
                    if (evt_queue_recv.comp_id == COMP_ID_MAIN)
                    {
                        switch(evt_queue_recv.event_id)
                        {
                            case(EVT_OTA_GET_VERS_ERROR):
                            {
                                ESP_LOGE(DIRECTOR_TAG, "OTA Check failed EVT");
                                current_state = STATE_ERROR;
                                state_update_led(*task_cfg_p->led_task, current_state);
                            }
                            break;
                            case(EVT_OTA_FW_UP_TO_DATE):
                            {
                                ESP_LOGI(DIRECTOR_TAG, "Latest FW, going back to IDLE");
                                current_state = STATE_IDLE;
                                state_update_led(*task_cfg_p->led_task, current_state);
                            }
                            break;
                            case(EVT_OTA_VERSION_UPDATE_READY):
                            {
                                ESP_LOGI(DIRECTOR_TAG, "OTA FW Update available, starting update");
                                current_state = STATE_OTA_UPDATING;
                                state_update_led(*task_cfg_p->led_task, current_state);

                                generic_event_t ota_start_evt = 
                                {
                                    .comp_id = COMP_ID_MAIN,
                                    .event_id = EVT_OTA_UPDATE_STARTING,
                                    .param = 0
                                };
                                BaseType_t upd_start_q_ret = xQueueSend(*task_cfg_p->evt_queue, &ota_start_evt, 0);
                                if (upd_start_q_ret != pdTRUE)
                                {
                                    ESP_LOGE(DIRECTOR_TAG, "Failed to write to evt queue from OTA UPDATE READY\r\n");
                                    break;
                                }
                            }
                            break;
                            default:
                            {
                                ESP_LOGE(DIRECTOR_TAG, "Unsupported OTA EVT");
                            }
                            break;
                        }
                    }
                    else
                    {
                        ESP_LOGE(DIRECTOR_TAG, "Unsupported OTA EVT Source");
                    }
                }
                break;
                case (STATE_OTA_UPDATING):
                {
                    if (evt_queue_recv.comp_id == COMP_ID_MAIN)
                    {
                        switch(evt_queue_recv.event_id)
                        {
                            case(EVT_OTA_UPDATE_STARTING):
                            {
                                esp_err_t update_ret = execute_https_ota_update();
                                if (update_ret != ESP_OK)
                                {
                                    ESP_LOGE(DIRECTOR_TAG, "Failed to exec update, retcode = %s", esp_err_to_name(update_ret));
                                    current_state = STATE_ERROR;
                                    state_update_led(*task_cfg_p->led_task, current_state);
                                    break;
                                }
                            }
                            break;
                            default:
                            {
                                ESP_LOGE(DIRECTOR_TAG, "Only 1 OTA_UPDATING EVT is suppported");
                            }
                            break;
                        }
                    }
                    
                }
                break;
                case (STATE_ERROR):
                {
                    if (evt_queue_recv.comp_id == COMP_ID_BUTTON)
                    {
                        switch(evt_queue_recv.event_id)
                        {
                            case(EVT_BUTTON_SHORT_CLICK):
                            {
                                ESP_LOGI(DIRECTOR_TAG, "BTN Interrupt, getting out of ERR State\r\n");
                                current_state = STATE_IDLE;
                                state_update_led(*task_cfg_p->led_task, STATE_IDLE);
                            }
                            break;
                            default:
                            {
                                ESP_LOGE(DIRECTOR_TAG, "Unsupported ERROR BTN EVT!\r\n");
                            }
                            break;
                        }
                    }
                    else
                    {
                        ESP_LOGE(ERROR_TAG, "Unsupported ERR EVT");
                    }
                }
                break;
                default:
                    break;
            }
        }
    }
}


static adc_digi_output_data_t adc_dma_chunk_buf[ADC_DMA_SINGLE_FRAME_LEN * SOC_ADC_DIGI_DATA_BYTES_PER_CONV] = {0, };

//using 4 bytes for a 12bit precision is excessive, ram is limited
static uint16_t adc_data_buf[ADC_MAX_READINGS * SOC_ADC_DIGI_DATA_BYTES_PER_CONV] = {0, };

uint32_t adc_single_frame_size      = ADC_DMA_SINGLE_FRAME_LEN * SOC_ADC_DIGI_DATA_BYTES_PER_CONV;
uint32_t adc_max_multiframe_len     = ADC_MAX_READINGS * SOC_ADC_DIGI_DATA_BYTES_PER_CONV;

measure_task_congif_t measure_cfg_str = 
{
    .adc_config = 
    {
        .adc_unit                   = ADC_UNIT_1,
        .adc_ch                     = ADC_CHANNEL_0,
        .adc_atten                  = ADC_ATTEN_DB_12,
        .output_type                = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
        .sample_freq                = 1000,
        
        .adc_dma_frame              = adc_dma_chunk_buf,
        .single_conv_frame_size     = ADC_DMA_SINGLE_FRAME_LEN * SOC_ADC_DIGI_DATA_BYTES_PER_CONV,
        //buffer includes possible overflow scenario
        .frame_buffer_size          = 4 * ADC_DMA_SINGLE_FRAME_LEN * SOC_ADC_DIGI_DATA_BYTES_PER_CONV,

        .adc_multiframe_buf         = adc_data_buf,
        .max_readings_num           = ADC_MAX_READINGS,
        
        .conv_done_cb_func          = NULL,
        .pool_ovf_cb_func           = NULL,
        .adc_task                   = &measure_task_h,

    },
    .evt_queue_h = &xDirectorEvtQueue,
};

director_task_config_t director_cfg_str = 
{
    .btn_task       = &button_task_h,
    .led_task       = &led_task_h,
    .measure_task   = &measure_task_h,
    .evt_queue      = &xDirectorEvtQueue
    
};

button_task_config_t button_cfg_str = 
{
    .evt_queue_p = &xDirectorEvtQueue,
    .btn_config_str = 
    {
        .btn_pin = GPIO_NUM_13,
        .integrator = 0,
        .long_press_period_ms = 3000,
        .short_press_period_ms = 200,
        .debounce_samples = 10,
        .polling_period_ms = 10,
    },
};

void app_main(void)
{
    ESP_LOGI(INIT_TAG, "%s\r\n", version_string);
    /* Configure the peripheral according to the LED type */

    xDirectorEvtQueue = xQueueCreate(10, sizeof(generic_event_t));
    if (xDirectorEvtQueue == NULL)
    {
        ESP_LOGE(INIT_TAG, "queue creation failed!\r\n");
        while(1);
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

    BaseType_t director_ret = xTaskCreatePinnedToCore(  director_task, 
                                                        "director task", 
                                                        4096, 
                                                        &director_cfg_str, 
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

    generic_event_t start_init_evt = 
    {
        .comp_id = COMP_ID_MAIN,
        .event_id = INIT_STARTED,
        .param = 0
    };

    BaseType_t init_q_start_ret = xQueueSend(xDirectorEvtQueue, &start_init_evt, 10);
    if (init_q_start_ret != pdTRUE)
    {
        ESP_LOGE(INIT_TAG, "Failed to send INIT START EVT\r\n");
    }

    BaseType_t btn_ret = xTaskCreatePinnedToCore(button_task_handler, 
                                                "Btn task", 
                                                4096, 
                                                &button_cfg_str, 
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
                                                    "Measure task", 
                                                    4096, 
                                                    &measure_cfg_str, 
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

    generic_event_t end_init_evt = 
    {
        .comp_id = COMP_ID_MAIN,
        .event_id = INIT_ENDED,
        .param = 0
    };

    BaseType_t end_q_end_ret = xQueueSend(xDirectorEvtQueue, &end_init_evt, 10);
    if (end_q_end_ret != pdTRUE)
    {
        ESP_LOGE(INIT_TAG, "Failed to send INIT END EVT\r\n");
    }

    return;
}
