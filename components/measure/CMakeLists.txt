idf_component_register(SRCS "user_wifi.c" "measure.c"
                    INCLUDE_DIRS "include"
                    REQUIRES common driver esp_http_client esp_wifi esp_timer esp_adc nvs_flash)
