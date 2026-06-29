idf_component_register(SRCS "measure.c"
                    INCLUDE_DIRS "include"
                    REQUIRES common driver esp_timer esp_adc user_wifi)
