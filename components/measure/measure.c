#include <stdio.h>
#include "measure.h"


static const char *ADC_TAG = "ADC";
static const char *WIFI_TAG = "WIFI";



static void init_adc_continuous(adc_continuous_handle_t *out_handle, user_adc_t *user_adc_p)
{
    // 1. Allocate the continuous mode master handle
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 4096, // Internal pool for DMA descriptors
        .conv_frame_size = user_adc_p->frame_size,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, out_handle));

    // 2. Configure the hardware pattern table and digital controller
    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = user_adc_p->sample_freq,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1, // Track Unit 1 only
        .format = user_adc_p->output_type,
    };

    // Define channel patterns to read
    adc_digi_pattern_config_t pattern_list[1] = {
        {
            .atten = user_adc_p->adc_atten,
            .channel = user_adc_p->adc_ch,
            .unit = user_adc_p->adc_unit,
            .bit_width = ADC_BITWIDTH_12,
        }
    };

    dig_cfg.pattern_num = 1;
    dig_cfg.adc_pattern = pattern_list;
    ESP_ERROR_CHECK(adc_continuous_config(*out_handle, &dig_cfg));

    // 3. Register asynchronous conversion event callbacks
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = user_adc_p->callback_func,
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(*out_handle, &cbs, (void *)user_adc_p));
}

//user data is registered in adc_continuous_register_event_callbacks, i use user_adc_t pointer
static bool IRAM_ATTR adc_coexist_cb(adc_continuous_handle_t handle, 
                                     const adc_continuous_evt_data_t *edata, 
                                     void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    user_adc_t *adc_str_p = (user_adc_t *)user_data;
    // Notify the processing task that new data is available in the DMA buffer
    xTaskNotifyFromISR(*adc_str_p->adc_task, (1 << MEASURE_BUF_FULL), eSetBits, &high_task_wakeup);
    return high_task_wakeup;
}

typedef struct
{
    QueueHandle_t   evt_queue_h;
    user_adc_t      adc_config;

}measure_task_congif_t;


void measure_task_handler(void *pvParameters)
{
    ESP_LOGI(ADC_TAG, "Measure task started\r\n");

    if (pvParameters == NULL)
    {
        ESP_LOGE(ADC_TAG, "MEASURE TASK ARG NULLPTR!\r\n");
        while (1);
    }
    measure_task_congif_t *measure_task_cfg = (measure_task_congif_t *)pvParameters;

    if (measure_task_cfg->evt_queue_h == NULL)
    {
        ESP_LOGE(ADC_TAG, "MEASURE EVT QUEUE NULLPTR!\r\n");
        while (1);
    }
    QueueHandle_t measure_evt_queue_h = measure_task_cfg->evt_queue_h;

    adc_continuous_handle_t adc_h = NULL;

    user_adc_t user_adc_str = 
    {
        .adc_unit = ADC_UNIT_1,
        .adc_ch = ADC_CHANNEL_0,
        .adc_atten = ADC_ATTEN_DB_12,
        .output_type = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
        .sample_freq = 1000,
        .frame_size = 10000,
        .callback_func = adc_coexist_cb,

    };

    adc_continuous_handle_cfg_t adc_config = 
    {
        .conv_frame_size = user_adc_str.frame_size,
        .max_store_buf_size = (user_adc_str.frame_size / 1024 + 1) * 1024, //give some headroom
    };

    init_adc_continuous(&adc_h, &user_adc_str);

    uint32_t measure_notify_val = 0;

    while (1)
    {
        UBaseType_t adc_wait_ret = xTaskNotifyWait(0x00, ULONG_MAX, &measure_notify_val, portMAX_DELAY);
        if (adc_wait_ret == pdPASS)
        {
            switch(measure_notify_val)
            {
                case(MEASURE_START):
                {

                }
                break;
                case(MEASURE_BTN_INTERRUPT):
                {
                    //notify from Director task
                    const uint32_t btn_evt = EVT_ADC_BTN_INTERRUPT;
                    adc_continuous_stop(adc_h);
                    BaseType_t full_queue_ret = xQueueSend(measure_evt_queue_h, &btn_evt, 0);
                    if (full_queue_ret != pdTRUE)
                    {
                        ESP_LOGE(ADC_TAG, "Failed to write to evt queue from BUF_FULL\r\n");
                        break;
                    }
                }
                break;
                case(MEASURE_BUF_FULL):
                {
                    //notify from ADC ISR
                    const uint32_t full_evt = EVT_ADC_FULL_INTERRUPT;
                    adc_continuous_stop(adc_h);
                    BaseType_t full_queue_ret = xQueueSend(measure_evt_queue_h, &full_evt, 0);
                    if (full_queue_ret != pdTRUE)
                    {
                        ESP_LOGE(ADC_TAG, "Failed to write to evt queue from BUF_FULL\r\n");
                        break;
                    }
                }
                break;
                case(MEASURE_START_UPLOAD):
                {

                }
                break;
            }
        }
    }
}