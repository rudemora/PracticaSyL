#include "nvs_flash.h"
#include "nvs.h"
#include "manage_nvs.h"
#include <stdio.h>
#include <string.h>


char access_token[128] = {0};

const char* get_token(void) {
    return access_token; 
}

void save_token_to_nvs(const char *token) {
    nvs_handle_t my_handle;
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &my_handle));
    ESP_ERROR_CHECK(nvs_set_str(my_handle, "tb_token", token));
    ESP_ERROR_CHECK(nvs_commit(my_handle));
    nvs_close(my_handle);
}

bool load_token_from_nvs() {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err != ESP_OK) return false;
    
    size_t required_size;
    err = nvs_get_str(my_handle, "tb_token", NULL, &required_size);
    if (err == ESP_OK && required_size < sizeof(access_token)) {
        nvs_get_str(my_handle, "tb_token", access_token, &required_size);
        nvs_close(my_handle);
        return true; 
    }
    nvs_close(my_handle);
    return false;
}