idf_component_register(SRCS "user_wifi.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi nvs_flash esp_http_client)
