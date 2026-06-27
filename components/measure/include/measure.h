#ifndef MEASURE_H
#define MEASURE_H

#include <stdint.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "esp_http_client.h"

#include "system_events.h"

typedef enum 
{
    EVT_ADC_BTN_INTERRUPT = 20,
    EVT_ADC_FULL_INTERRUPT,
    EVT_WIFI_CONNECTED,
    EVT_WIFI_ERROR,
    EVT_UPLOAD_DONE,
    EVT_UPLOAD_ERROR
}measure_event_id_t;

typedef enum
{
    MEASURE_START = 0,
    MEASURE_BTN_INTERRUPT,
    MEASURE_BUF_FULL,
    MEASURE_START_UPLOAD
}measure_cmd_t;

//struct for single channel ADC use
typedef struct
{
    uint32_t frame_size;
    uint32_t sample_freq;
    adc_digi_output_format_t output_type;//type 2 for esp32s3
    uint8_t adc_atten;
    uint8_t adc_ch;
    uint8_t adc_unit;

    uint16_t *adc_data;

    TaskHandle_t *adc_task;

    adc_continuous_callback_t callback_func;
}user_adc_t;


void measure_task_handler(void *pvParameters);



#endif