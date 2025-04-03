idf_component_register(SRCS "esp-cs110.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES 
                        "esp_driver_gpio"
                        "esp_driver_uart"
                        "esp_wifi"
                        "nvs_flash"
                        "mqtt"
                        "protocol_examples_common")
