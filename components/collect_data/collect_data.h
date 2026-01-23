#ifndef COLLECT_DATA_H
#define COLLECT_DATA_H

#include <stdint.h>


void init_temp_sensor();
void obtener_datos(float *temp, uint32_t *heap, int8_t *rssi);


#endif 