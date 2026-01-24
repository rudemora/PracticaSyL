#ifndef MQTT_TB_H
#define MQTT_TB_H

#include <stdint.h>


int get_intervalo_envio_mqtt();
int tb_mqtt_send_telemetry(float mcu_temp, uint32_t free_heap, int8_t rssi);
void mqtt_app_start(char* device_name);

#endif 