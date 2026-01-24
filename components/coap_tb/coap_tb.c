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
#define PROVISION_KEY      CONFIG_PROVISION_KEY_COAP 
#define PROVISION_SECRET   CONFIG_PROVISION_SECRET_COAP

// Certificados
extern const uint8_t root_server_coap_pem_start[] asm("_binary_server_coap_pem_start");
extern const uint8_t root_server_coap_pem_end[]   asm("_binary_server_coap_pem_end");

static const char *TAG = "coap_tb";
static int intervalo_envio = 5000;
static coap_session_t *coap_session = NULL;
static coap_context_t *coap_ctx = NULL;
static bool is_provisioning = false;

int get_intervalo_envio_coap() { return intervalo_envio; }


static char g_device_name[64];


extern void actualizar_timer_intervalo(int nuevo_ms);



/**
 * @brief Solicita el atributo 'intervalo_envio' al servidor
 * Endpoint: GET coap://host/api/v1/$TOKEN/attributes?sharedKeys=intervalo_envio
 */


void tb_coap_subscribe_attributes() {
    if (!coap_session || is_provisioning || get_token() == NULL) return;

    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_GET, coap_session);
    if (!pdu) return;

    // 1. Añadir opción OBSERVE (valor 0 para iniciar observación)
    uint8_t obs_buf[3];
    coap_add_option(pdu, COAP_OPTION_OBSERVE, coap_encode_var_safe(obs_buf, sizeof(obs_buf), COAP_OBSERVE_ESTABLISH), obs_buf);

    // 2. URI PATH para suscripción en ThingsBoard
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 3, (const uint8_t *)"api");
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 2, (const uint8_t *)"v1");
    coap_add_option(pdu, COAP_OPTION_URI_PATH, strlen(get_token()), (const uint8_t *)get_token());
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 10, (const uint8_t *)"attributes");
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 9, (const uint8_t *)"subscribe"); // Endpoint de suscripción

    coap_send(coap_session, pdu);
    ESP_LOGI(TAG, "Suscrito a cambios de atributos (Observe) via CoAP...");
}

/**
 * @brief Manejador de respuestas del servidor (ACKs y Datos)
 */
static coap_response_t message_handler(coap_session_t *session, const coap_pdu_t *sent, 
                                     const coap_pdu_t *received, const coap_mid_t id) {
    coap_pdu_code_t rcv_code = coap_pdu_get_code(received);
    size_t len;
    const uint8_t *data;

    if (rcv_code == COAP_RESPONSE_CODE_CREATED || 
        rcv_code == COAP_RESPONSE_CODE_CHANGED || 
        rcv_code == COAP_RESPONSE_CODE_CONTENT) {
        
        coap_get_data(received, &len, &data);
        if (len == 0) return COAP_RESPONSE_OK;

        cJSON *root = cJSON_ParseWithLength((const char *)data, len);
        if (!root) return COAP_RESPONSE_OK;

        if (is_provisioning) {
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
        } else {
            // Buscamos el atributo en la respuesta (puede venir en "shared" o directo)
            cJSON *shared = cJSON_GetObjectItem(root, "shared");
            cJSON *intervalItem = shared ? cJSON_GetObjectItem(shared, "intervalo_envio") : 
                                           cJSON_GetObjectItem(root, "intervalo_envio");

            if (intervalItem && cJSON_IsNumber(intervalItem)) {
                intervalo_envio = intervalItem->valueint;
                ESP_LOGI(TAG, "Intervalo actualizado desde CoAP: %d ms", intervalo_envio);
                actualizar_timer_intervalo(intervalo_envio);
            } else {
                ESP_LOGI(TAG, "Confirmación de telemetría CoAP (ACK)");
            }        }
        cJSON_Delete(root);

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
    pki_info.pki_key.key.pem_buf.ca_cert = root_server_coap_pem_start;
    pki_info.pki_key.key.pem_buf.ca_cert_len = root_server_coap_pem_end - root_server_coap_pem_start;
    
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

    char payload[256];
            snprintf(payload, sizeof(payload), 
                "{\"deviceName\": \"%s\", \"provisionDeviceKey\": \"%s\", \"provisionDeviceSecret\": \"%s\"}",
                g_device_name, PROVISION_KEY, PROVISION_SECRET);
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
        vTaskDelay(pdMS_TO_TICKS(1500));
        tb_coap_subscribe_attributes();
    } else {
        ESP_LOGW(TAG, "Sin Token. Iniciando Provisioning CoAP...");
        strncpy(g_device_name, device_name, sizeof(g_device_name)-1);
        tb_coap_provision();
        is_provisioning = true;
    }
}





void coap_tb_process() {
    if (coap_ctx) coap_io_process(coap_ctx, 100);
}

esp_err_t tb_coap_send_telemetry(float mcu_temp, uint32_t free_heap, int8_t rssi) {
    if (is_provisioning || !coap_session || get_token() == NULL) return ESP_FAIL;

    char payload[128]; 
    int len = snprintf(payload, sizeof(payload), 
                "{\"mcu_temp\": %.2f, \"free_heap\": %u, \"rssi\": %d}", 
                mcu_temp, (unsigned int)free_heap, (int)rssi);


    coap_pdu_t *pdu = coap_new_pdu(COAP_MESSAGE_CON, COAP_REQUEST_POST, coap_session);
    if (!pdu) return ESP_FAIL;

    uint8_t buf[2];
    coap_add_option(pdu, COAP_OPTION_CONTENT_FORMAT, coap_encode_var_safe(buf, sizeof(buf), COAP_MEDIATYPE_APPLICATION_JSON), buf);

    coap_add_option(pdu, COAP_OPTION_URI_PATH, 3, (const uint8_t *)"api");
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 2, (const uint8_t *)"v1");
    coap_add_option(pdu, COAP_OPTION_URI_PATH, strlen(get_token()), (const uint8_t *)get_token());
    coap_add_option(pdu, COAP_OPTION_URI_PATH, 9, (const uint8_t *)"telemetry");
    
    coap_add_data(pdu, len, (uint8_t *)payload);

    coap_send(coap_session, pdu);
    ESP_LOGI(TAG, "Enviando telemetria COAP...");
    return ESP_OK;
}