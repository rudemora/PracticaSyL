#include "mqtt_tb.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "manage_nvs.h"
#include "telemetry.pb.h"
#include "pb_encode.h"


#define PROVISION_KEY     CONFIG_PROVISION_KEY_MQTT   
#define PROVISION_SECRET  CONFIG_PROVISION_SECRET_MQTT 
#define BROKER_URL        CONFIG_BROKER_URL

extern const uint8_t root_server_mqtt_pem_start[] asm("_binary_server_mqtt_pem_start"); 

extern void actualizar_timer_intervalo(int nuevo_ms);

static const char *TAG = "mqtt_tb";
static int intervalo_envio = 5000;

int get_intervalo_envio_mqtt(){
    return intervalo_envio;
}


static char g_device_name[64];
bool is_provisioning_mode = false;
esp_mqtt_client_handle_t client; 


static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Ultimo error %s: 0x%x", message, error_code);
    }
}


static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Evento del event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    client = event->client;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: //conexión al broker
        ESP_LOGI(TAG, "MQTT Connected");
        
        if (is_provisioning_mode) {
            // Subscribirnos en el topic de respuesta del servidor
            esp_mqtt_client_subscribe(client, "/provision/response/+", 1);
            // Preparar request con los datos de nuestro dispositivo 
            char payload[256];
            snprintf(payload, sizeof(payload), 
                "{\"deviceName\": \"%s\", \"provisionDeviceKey\": \"%s\", \"provisionDeviceSecret\": \"%s\"}",
                g_device_name, PROVISION_KEY, PROVISION_SECRET);

            // Enviar request al servidor
            esp_mqtt_client_publish(client, "/provision/request", payload, 0, 0, 0);
            ESP_LOGI(TAG, "Enviada request de aprovisionamiento...");
        } else {
            // Subscribirnos al topic de actualizaciones de atributos (para cambios en tiempo real)
            esp_mqtt_client_subscribe(client, "v1/devices/me/attributes", 1);

            // Subscribirnos al topic de respuesta de actualizaciones de atributos (lectura inicial)
            esp_mqtt_client_subscribe(client, "v1/devices/me/attributes/response/+", 1);
            
            const char *request_payload = "{\"sharedKeys\":\"intervalo_envio\"}";
            esp_mqtt_client_publish(client, "v1/devices/me/attributes/request/1", request_payload, 0, 1, 0);
                    
            ESP_LOGI(TAG, "Petición de atributos enviada...");

            ESP_LOGI(TAG, "Subscrito a los atributos");
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA: // recibimos datos en un topic suscrito
         if (is_provisioning_mode) {
            ESP_LOGI(TAG, "Respuesta de aprovisionamiento: %.*s\r\n", event->data_len, event->data);
            
            // parseamos JSON para obtener credentialsValue
            cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
            cJSON *status = cJSON_GetObjectItem(root, "status");
            
            if (status && strcmp(status->valuestring, "SUCCESS") == 0) {
                cJSON *tokenItem = cJSON_GetObjectItem(root, "credentialsValue");
                if (tokenItem) {
                    ESP_LOGI(TAG, "Token Recibido: %s", tokenItem->valuestring);
                    
                    //Guardar en NVS
                    save_token_to_nvs(tokenItem->valuestring);
                    
                    //RESTART para cargar el token y ejecutar el código con provisioningMode = False
                    ESP_LOGI(TAG, "Reiniciando en 2 segundos...");
                    vTaskDelay(2000 / portTICK_PERIOD_MS);
                    esp_restart();
                }
            }
            cJSON_Delete(root);
        }
        else {
            ESP_LOGI(TAG, "Recibidos datos: %.*s\r\n", event->data_len, event->data);
        
            // Parsear JSON
            cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
            if (root == NULL) break;

            cJSON *intervalItem = cJSON_GetObjectItem(root, "intervalo_envio"); 
            
            if (!intervalItem) { 
                cJSON *shared = cJSON_GetObjectItem(root, "shared");
                if (shared) {
                    intervalItem = cJSON_GetObjectItem(shared, "intervalo_envio");
                }
            }

            if (intervalItem && cJSON_IsNumber(intervalItem)) { 
                intervalo_envio = intervalItem->valueint;
                ESP_LOGI(TAG, "NUEVO INTERVALO: %d ms", intervalo_envio);
                actualizar_timer_intervalo(intervalo_envio);
            }

            cJSON_Delete(root);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}




void mqtt_app_start(char* device_name)
{
    // Try to load token
    if (load_token_from_nvs()) {
        ESP_LOGI(TAG, "Token found in NVS: %s", get_token());
        is_provisioning_mode = false;
    } else {
        ESP_LOGW(TAG, "No token found. Starting Provisioning Mode.");
        strncpy(g_device_name, device_name, sizeof(g_device_name)-1);
        g_device_name[sizeof(g_device_name) - 1] = '\0';
        is_provisioning_mode = true;
    }
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = BROKER_URL,
                // certificado
        .broker.verification.certificate = (const char *)root_server_mqtt_pem_start, 
        

        .broker.verification.skip_cert_common_name_check = true,
        .credentials.username = is_provisioning_mode ? "provision" : get_token(), 
        .network.timeout_ms = 10000,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}


int tb_mqtt_send_telemetry(float mcu_temp, uint32_t free_heap, int8_t rssi) {
    if (client == NULL || is_provisioning_mode) {
        return ESP_FAIL;
    }

    uint8_t buffer[128];
    telemetry_SensorDataReading message = telemetry_SensorDataReading_init_default;

    // Asignación de datos
    message.mcu_temp = (double)mcu_temp;
    message.rssi = (int32_t)rssi;
    message.free_heap = free_heap;

    // Codificación Protobuf
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    if (!pb_encode(&stream, telemetry_SensorDataReading_fields, &message)) {
        ESP_LOGE(TAG, "Error codificando Protobuf");
        return ESP_FAIL;
    }

    // Publicación
    int msg_id = esp_mqtt_client_publish(client, "v1/devices/me/telemetry", 
                                        (const char *)buffer, stream.bytes_written, 1, 0);
    if (msg_id != -1) {
        ESP_LOGI(TAG, "Publicacion exitosa");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Error al publicar");
        return ESP_FAIL;
    }
}