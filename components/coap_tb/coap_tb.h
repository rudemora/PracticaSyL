#ifndef COAP_TB_H
#define COAP_TB_H

#include <stdint.h>
#include "esp_err.h"

// Getters para el main
int get_intervalo_envio_coap();

// Inicialización y envío
void coap_app_start(char* device_name);
esp_err_t tb_coap_send_telemetry(float mcu_temp, uint32_t free_heap, int8_t rssi);
void coap_tb_process();

#endif