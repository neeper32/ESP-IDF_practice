idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_adc esp_timer esp_driver_i2c esp_event nvs_flash esp_wifi esp_http_server)