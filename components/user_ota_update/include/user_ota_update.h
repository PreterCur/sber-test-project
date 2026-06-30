#ifndef USER_OTA_UPDATE_H
#define USER_OTA_UPDATE_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"

#include "esp_timer.h"

#include "system_events.h"
#include "user_wifi.h"

#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"

typedef enum
{
    EVT_OTA_GET_VERS_ERROR,
    EVT_OTA_FW_UP_TO_DATE,
    EVT_OTA_VERSION_UPDATE_READY,
    EVT_OTA_UPDATE_STARTING

}ota_event_id_t;

#define VERSION_CHECK_URL       "http://your-server.com/api/version"
#define FIRMWARE_DOWNLOAD_URL   "https://your-server.com/bin/firmware.bin"


esp_err_t get_available_version(char *version_buf, size_t max_len);
esp_err_t execute_https_ota_update(void);


#endif