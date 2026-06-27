idf_component_register(SRCS "measure.c"
                    INCLUDE_DIRS "include"
                    REQUIRES common driver esp_http_client esp_wifi esp_timer esp_adc)
