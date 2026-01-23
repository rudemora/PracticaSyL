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

#include "pb_decode.h"
#include "attributes.pb.h"

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

/*void publisher(void *pvParameters) {
    int msg_id;
    
    srand(time(NULL));
    vTaskDelay(500 / portTICK_PERIOD_MS);

    // ThingsBoard requires this specific topic for telemetry
    const char *tb_topic = "v1/devices/me/telemetry";
    //wifi_ap_record_t *ap;
    while (1) {
        if (!is_provisioning_mode && client != NULL) {
            int8_t rssi;
            uint32_t heap;
            obtener_datos(&rssi, &heap);

            float mcu_temp = obtener_temperatura_mcu();

            char payload[256]; 
            snprintf(payload, sizeof(payload), 
                     "{\"mcu_temp\": %.2f, \"free_heap\": %" PRIu32 ", \"rssi\": %d}", 
                     mcu_temp, heap, rssi);
            //
            float temp, hum, lux, vibr;
            generar_datos(&temp, &hum, &lux, &vibr);
            //ap = esp_wifi_sta_get_ap_info(ap);
            // Create a JSON string. 
            // Example output: {"temperature": 23.5, "humidity": 50.2, "lux": 500.0, "vibration": 1.2}
            char payload[128]; 
            snprintf(payload, sizeof(payload), 
                     "{\"temperature\": %.2f, \"humidity\": %.2f, \"lux\": %.2f, \"vibration\": %.2f}", 
                     temp, hum, lux, vibr);
            //
            // Publish to ThingsBoard
            // QoS 0 or 1 is fine. Retain should usually be 0 for telemetry.
            msg_id = esp_mqtt_client_publish(client, tb_topic, payload, 0, 1, 0);
            
            ESP_LOGI(TAG, "Sent JSON to ThingsBoard: %s", payload);
        } else {
            ESP_LOGI(TAG, "Sensor disabled, skipping publish");
        }
        
        vTaskDelay(intervalo_envio / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}*/



/*void publisher(void *pvParameters)
{
    uint8_t buffer[128]; // Buffer para Protobuf binario

    while (1) {
        if (!is_provisioning_mode && client != NULL) {

            // 1. Obtener datos
            float mcu_temp = obtener_temperatura_mcu();
            uint32_t heap = esp_get_free_heap_size();

            int8_t rssi = 0;
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                rssi = ap_info.rssi;
            }

            // 2. Crear mensaje Protobuf (nanopb)
            telemetry_SensorDataReading message =
                telemetry_SensorDataReading_init_default;

            message.mcu_temp = (double)mcu_temp;
            message.rssi = (int32_t)rssi;
            message.free_heap = heap;

            // 3. Codificar a binario
            pb_ostream_t stream =
                pb_ostream_from_buffer(buffer, sizeof(buffer));

            if (!pb_encode(&stream,
                            telemetry_SensorDataReading_fields,
                            &message)) {
                ESP_LOGE(TAG, "Error al codificar Protobuf: %s",
                         PB_GET_ERROR(&stream));
                vTaskDelay(pdMS_TO_TICKS(intervalo_envio));
                continue;
            }

            size_t len = stream.bytes_written;

            // 4. PUBLICAR en el topic CORRECTO de ThingsBoard
            esp_mqtt_client_publish(
                client,
                "v1/devices/me/telemetry", // 
                (const char *)buffer,
                len,   // 
                1,     // QoS recomendado
                0
            );

            ESP_LOGI(TAG,
                     "Enviado Protobuf (%d bytes). Temp: %.2f",
                     (int)len, mcu_temp);
        }

        vTaskDelay(pdMS_TO_TICKS(intervalo_envio));
    }
}*/

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



/*void publisher(void *pvParameters) { //COAP
    float temp; uint32_t heap; int8_t rssi;
    while (1) {
        // La tarea se queda aquí bloqueada hasta que el timer avise
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGI(TAG, "Publisher despertado");

        // Obtener datos
        obtener_datos(&temp, &heap, &rssi);

        // Enviar datos
        tb_coap_send_telemetry(temp, heap, rssi);
     
        ESP_LOGI(TAG, "Envío completado. Volviendo a dormir");
    }

}*/

void publisher(void *pvParameters) { //MQTT
    float temp; uint32_t heap; int8_t rssi;
    while (1) {
        // La tarea se queda aquí bloqueada hasta que el timer avise
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // obtener datos
        obtener_datos(&temp, &heap, &rssi);

        // enviar datos 
        if (tb_mqtt_send_telemetry(temp, heap, rssi) == ESP_OK) { //CAMBIO
            ESP_LOGI("APP", "Telemetria enviada correctamente");
        } else {
            ESP_LOGW("APP", "Ningun envío realizado (modo provisioning o desconectado)");
        }


        vTaskDelay(pdMS_TO_TICKS(get_intervalo_envio()));
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
    //xTaskCreate(&coap_process_task, "coap_process", 4096, NULL, 6, NULL); //mas prioridad
    //coap_app_start(device_name);
    mqtt_app_start(device_name);

    const esp_timer_create_args_t coap_timer_args = {
        .callback = &sensor_timer_callback,
        .name = "periodic_timer"
    };

    ESP_ERROR_CHECK(esp_timer_create(&coap_timer_args, &coap_timer));


    int64_t periodo_us = (int64_t)get_intervalo_envio() * 1000;
    ESP_ERROR_CHECK(esp_timer_start_periodic(coap_timer, periodo_us));
    xTaskCreate(publisher, "publisher", 4096, NULL, 5, &xPublisherTask);

    ESP_LOGI(TAG, "Sistema listo. Timer configurado cada %d ms", get_intervalo_envio());
}
