#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"

#include "esp_timer.h"

#include "measure.h"
#include "button.h"
#include "user_led.h"

#include "sdkconfig.h"

typedef enum
{
    STATE_INIT = 0,
    STATE_IDLE,
    STATE_MEASURING,
    STATE_WIFI_CONNECTING,
    STATE_UPLOADING,
    STATE_OTA_CHECKING,
    STATE_OTA_UPDATING,
    STATE_ERROR
}system_state_t;

typedef enum
{
    INIT_STARTED = 0,
    INIT_ENDED = 1,
}init_event_id_t;

typedef struct 
{
    QueueHandle_t   *evt_queue;
    TaskHandle_t    *led_task;
    TaskHandle_t    *btn_task;
    TaskHandle_t    *measure_task;

}director_task_config_t;



#endif