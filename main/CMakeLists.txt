idf_component_register(SRCS "app_main.c" "pb_decode.c" "attributes.pb.c"
                    PRIV_REQUIRES mqtt nvs_flash json esp_netif esp_wifi driver esp_driver_tsens  
                    REQUIRES collect_data mqtt_tb manage_nvs coap_tb esp_timer
                    INCLUDE_DIRS ".")
