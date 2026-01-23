#include "coap_tb.h"
#include "coap3/coap.h"
#include "esp_log.h"
#include "manage_nvs.h"
#include "telemetry.pb.h"
#include "pb_encode.h"
#include "cJSON.h"
#include <netdb.h>

// Configuraciones del Kconfig
#define BROKER_IP          CONFIG_BROKER_IP 
#define COAP_PORT          "5684"
#define PROVISION_KEY      CONFIG_PROVISION_KEY 
#define PROVISION_SECRET   CONFIG_PROVISION_SECRET

// Certificados
//extern const uint8_t root_server_pem_start[] asm("_binary_server_pem_start");
//extern const uint8_t root_server_pem_end[]   asm("_binary_server_pem_end");

static const char *TAG = "coap_tb";
static int intervalo_envio = 5000;
static coap_session_t *coap_session = NULL;
static coap_context_t *coap_ctx = NULL;
static bool is_provisioning = false;

int get_intervalo_envio() { return intervalo_envio; }

/**
 * @brief Manejador de respuestas del servidor (ACKs y Datos)
 */
static coap_response_t message_handler(coap_session_t *session, const coap_pdu_t *sent, 
                                     const coap_pdu_t *received, const coap_mid_t id) {
    coap_pdu_code_t rcv_code = coap_pdu_get_code(received);
    size_t len;
    const uint8_t *data;

    if (rcv_code == COAP_RESPONSE_CODE_CREATED || rcv_code == COAP_RESPONSE_CODE_CHANGED) {
        if (is_provisioning) {
            coap_get_data(received, &len, &data);
            cJSON *root = cJSON_ParseWithLength((const char *)data, len);
            cJSON *status = cJSON_GetObjectItem(root, "status");

            if (status && strcmp(status->valuestring, "SUCCESS") == 0) {
                cJSON *tokenItem = cJSON_GetObjectItem(root, "credentialsValue");
                if (tokenItem) {
                    ESP_LOGI(TAG, "¡TOKEN RECIBIDO!: %s", tokenItem->valuestring);
                    save_token_to_nvs(tokenItem->valuestring);
                    ESP_LOGI(TAG, "Reiniciando para aplicar cambios...");
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    esp_restart();
                }
            }
            cJSON_Delete(root);
        } else {
            ESP_LOGI(TAG, "Telemetría confirmada por ThingsBoard (ACK)");
        }
    } else {
        ESP_LOGW(TAG, "Error del servidor. Código: %d.%02d", rcv_code >> 5, rcv_code & 0x1F);
    }
    return COAP_RESPONSE_OK;
}

/**
 * @brief Configura la sesión DTLS con certificados
 */
static coap_session_t* setup_dtls_session() {
    coap_address_t dst;
    coap_addr_info_t *info_list = NULL;

    info_list = coap_resolve_address_info(coap_make_str_const(BROKER_IP), 0, atoi(COAP_PORT), 
                                         0, 0, AF_INET, COAP_PROTO_DTLS, COAP_RESOLVE_TYPE_LOCAL);
    if (!info_list) return NULL;
    dst = info_list->addr;
    coap_free_address_info(info_list);

    if (!coap_ctx) {
        coap_ctx = coap_new_context(NULL);
        coap_register_response_handler(coap_ctx, message_handler);
    }

    coap_dtls_pki_t pki_info;
    memset(&pki_info, 0, sizeof(pki_info));
    pki_info.version = COAP_DTLS_PKI_SETUP_VERSION;
    pki_info.pki_key.key_type = COAP_PKI_KEY_PEM_BUF;
    //pki_info.pki_key.key.pem_buf.ca_cert = root_server_pem_start;
    //pki_info.pki_key.key.pem_buf.ca_cert_len = root_server_pem_end - root_server_pem_start;
    
    pki_info.verify_peer_cert = 1; 
    pki_info.check_common_ca = 0;
    pki_info.allow_self_signed = 1;

    return coap_new_client_session_pki(coap_ctx, NULL, &dst, COAP_PROTO_DTLS, &pki_info);
}

/**
 * @brief Solicita un Token a ThingsBoard vía CoAP
 */
void tb_coap_provision() {
    is_provisioning = true;
    if (!coap_session) coap_session = setup_dtls_session();
    if (!coap_session) return;
    ESP_LOGW(TAG, "1");

    char payload[256];
    snprintf(payload, sizeof(payload), 
            "{\"deviceName\": \"ESP32_PROYECTO\", \"provisionDeviceKey\": \"%s\", \"provisionDeviceSecret\": \"%s\"}",
            PROVISION_KEY, PROVISION_SECRET);
    ESP_LOGW(TAG, "2");

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_POST, coap_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 3, (const uint8_t *)"api");
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 2, (const uint8_t *)"v1");
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 9, (const uint8_t *)"provision");
    coap_add_data(pdu, strlen(payload), (uint8_t *)payload);

    coap_send(coap_session, pdu);
    ESP_LOGI(TAG, "Petición de Provisioning enviada...");
}

void coap_app_start(char* device_name) {
    if (load_token_from_nvs()) {
        ESP_LOGI(TAG, "Token listo: %s. Iniciando Telemetría...", get_token());
        is_provisioning = false;
        coap_session = setup_dtls_session();
    } else {
        ESP_LOGW(TAG, "Sin Token. Iniciando Provisioning CoAP...");
        tb_coap_provision();
    }
}

void coap_tb_process() {
    if (coap_ctx) coap_io_process(coap_ctx, 100);
}

esp_err_t tb_coap_send_telemetry(float mcu_temp, uint32_t free_heap, int8_t rssi) {
    if (is_provisioning || !coap_session) return ESP_FAIL;

    uint8_t buffer[128];
    telemetry_SensorDataReading message = telemetry_SensorDataReading_init_default;
    message.mcu_temp = (double)mcu_temp;
    message.rssi = (int32_t)rssi;
    message.free_heap = free_heap;

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    if (!pb_encode(&stream, telemetry_SensorDataReading_fields, &message)) return ESP_FAIL;

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_POST, coap_session);
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 3, (const uint8_t *)"api");
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 2, (const uint8_t *)"v1");
    coap_add_option(pdu, COAP_OPTION_URI_PATH, strlen(get_token()), (const uint8_t *)get_token());
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 9, (const uint8_t *)"telemetry");
    coap_add_data(pdu, stream.bytes_written, buffer);

    coap_send(coap_session, pdu);
    ESP_LOGI(TAG, "Enviando telemetría (Protobuf)...");
    return ESP_OK;
}