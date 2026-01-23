idf_component_register(SRCS "manage_nvs.c"
                    REQUIRES nvs_flash esp_event
                    INCLUDE_DIRS ".")