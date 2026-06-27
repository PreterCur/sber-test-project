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




#endif