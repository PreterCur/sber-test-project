#ifndef MEASURE_H
#define MEASURE_H

#include <stdint.h>
#include <math.h>
#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "esp_http_client.h"

#include "system_events.h"

#include "user_wifi.h"


#define ADC_MAX_READINGS                    (uint32_t)10000
#define ADC_DMA_SINGLE_FRAME_LEN            (uint32_t)100


typedef enum 
{
    EVT_ADC_BTN_INTERRUPT = 20,
    EVT_ADC_FULL_INTERRUPT,
    EVT_ADC_OVERFLOW_ERR,
    EVT_CSV_CREATE_ERROR,
    EVT_WIFI_CONNECTED,
    EVT_WIFI_ERROR,
    EVT_UPLOAD_DONE,
    EVT_UPLOAD_ERROR
}measure_event_id_t;

typedef enum
{
    //notification cmds from director
    MEASURE_START = 0,
    MEASURE_BTN_INTERRUPT,
    MEASURE_FULL_INTERRUPT,
    MEASURE_GENERATE_CSV,
    MEASURE_START_UPLOAD,
    
    //notification from adc isr
    MEASURE_BUF_FULL_CALLBACK = 20,
    MEASURE_BUF_OVERFLOW_CALLBACK = 30,
}measure_cmd_t;

//struct for single channel ADC use
typedef struct
{
    adc_digi_output_data_t      *adc_dma_frame;
    uint32_t                    single_conv_frame_size;

    uint16_t                    *adc_multiframe_buf;//using 4 vytes for a massive buffer is excessive
    uint32_t                    frame_buffer_size;

    uint32_t                    sample_freq;
    adc_digi_output_format_t    output_type;//type 2 for esp32s3
    uint8_t                     adc_atten;
    uint8_t                     adc_ch;
    uint8_t                     adc_unit;

    uint32_t                    adc_read_num;

    TaskHandle_t                *adc_task;

    adc_continuous_callback_t   callback_func;
}user_adc_t;

typedef struct
{
    QueueHandle_t   *evt_queue_h;
    user_adc_t      adc_config;

}measure_task_congif_t;


void measure_task_handler(void *pvParameters);



#endif