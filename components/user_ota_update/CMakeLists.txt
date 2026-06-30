idf_component_register(SRCS "user_ota_update.c"
                    INCLUDE_DIRS "include"
                    REQUIRES common esp_timer user_wifi esp_app_format esp_https_ota app_update)
