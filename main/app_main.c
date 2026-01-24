#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include <time.h>
#include "cJSON.h"


#include "esp_wifi.h"


#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"


#include "nvs_flash.h"
#include "mqtt_tb.h"
#include "collect_data.h"
#include "coap_tb.h"
#include "esp_timer.h"





static const char *TAG = "app_main";


static esp_timer_handle_t coap_timer;
static TaskHandle_t xPublisherTask = NULL;


char device_name[64];

void generate_device_name() {
    uint8_t mac[6];

    // Obtener la MAC base del ESP32
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // Crear nombre único usando la MAC
    snprintf(device_name, sizeof(device_name),
             "ESP32_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
}


// callback del timer
static void sensor_timer_callback(void* arg)
{
    if (xPublisherTask != NULL) {
        // Enviamos una notificación a la tarea para que despierte
        xTaskNotifyGive(xPublisherTask);
    }
}

void coap_process_task(void *pvParameters) {
    while(1) {
        coap_tb_process();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void publisher(void *pvParameters) {
    float temp; uint32_t heap; int8_t rssi;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // Espera al timer

        obtener_datos(&temp, &heap, &rssi);

        // Envío por COAP
        //tb_coap_send_telemetry(temp, heap, rssi);

        // Envío por MQTT
        tb_mqtt_send_telemetry(temp, heap, rssi);
        ESP_LOGI(TAG, "ENVIO");
    }
}


// Reiniciar el timer con el nuevo tiempo de MQTT
void actualizar_timer_intervalo(int nuevo_ms) {
    if (coap_timer != NULL) {
        esp_timer_stop(coap_timer);
        int64_t periodo_us = (int64_t)nuevo_ms * 1000;
        esp_timer_start_periodic(coap_timer, periodo_us);
        ESP_LOGI("APP_MAIN", "Timer actualizado a %d ms por orden de MQTT", nuevo_ms);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());


    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

    generate_device_name();
    init_temp_sensor(); // Inicializar el sensor interno


    //Iniciar COAP
    //xTaskCreate(&coap_process_task, "coap_process", 4096, NULL, 6, NULL); //mas prioridad
    //coap_app_start(device_name);
    
    //Iniciar MQTT
    mqtt_app_start(device_name);

    const esp_timer_create_args_t coap_timer_args = {
        .callback = &sensor_timer_callback,
        .name = "periodic_timer"
    };

    ESP_ERROR_CHECK(esp_timer_create(&coap_timer_args, &coap_timer));


    int64_t periodo_us = (int64_t)get_intervalo_envio_mqtt() * 1000;
    //int64_t periodo_us = (int64_t)get_intervalo_envio_coap() * 1000;

    ESP_ERROR_CHECK(esp_timer_start_periodic(coap_timer, periodo_us));
    
    xTaskCreate(publisher, "publisher", 4096, NULL, 5, &xPublisherTask);

    ESP_LOGI(TAG, "Sistema listo. Timer configurado cada %d ms", get_intervalo_envio_mqtt());
    //ESP_LOGI(TAG, "Sistema listo. Timer configurado cada %d ms", get_intervalo_envio_coap());

}
