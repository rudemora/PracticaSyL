#include "collect_data.h"
#include "driver/temperature_sensor.h"
#include "esp_wifi.h"



temperature_sensor_handle_t temp_sensor = NULL;



void init_temp_sensor() {
    /* Las funciones install y enable estan soportadas para otras placas pero da undefined reference con la ESP32, dejamos la función aun así para 
       mantener formato habitual de inicialización de componente
    temperature_sensor_config_t temp_sensor_config = {
    .range_min = 10,
    .range_max = 50,
    };
    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_sensor)); //instalar driver temperatura
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor)); //habilitar sensor
    */
}


float obtener_temperatura_mcu() {
    return 17;
    float tsens_out;
    if (temp_sensor != NULL) {
        ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_sensor, &tsens_out));
        return tsens_out;
    }
    return 0.0;
}


void obtener_rssi_heap(int8_t *rssi, uint32_t *heap) {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        *rssi = ap_info.rssi;
    } else {
        *rssi = 0;
    }
    *heap = esp_get_free_heap_size();
}


void obtener_datos(float *temp, uint32_t *heap, int8_t *rssi) {
    *temp = obtener_temperatura_mcu(); //devuelto con return
    obtener_rssi_heap(rssi, heap); //por referencia
}