#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <sys/time.h>
#include <mdns.h>
#include <esp_http_server.h>
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"
#include "waterMonitor.h"

const char *MQTT_TAG = "waterMonitor_mqtt";
const char TAG[] = "waterMonitor";
esp_mqtt_client_handle_t client;
SemaphoreHandle_t xMQTTClientMutex;

// MQTT client configuration
extern const uint8_t m2mqtt_ca_pem_start[] asm("_binary_m2mqtt_ca_pem_start");
extern const uint8_t m2mqtt_ca_pem_end[]   asm("_binary_m2mqtt_ca_pem_end");
static void mqtt_app_start(void);

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    BaseType_t rc;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_CONNECTED");
            rc = xSemaphoreGive(xMQTTClientMutex);
            if (pdPASS != rc) {
                ESP_LOGW(MQTT_TAG, "Failed tp get MQTT Mutex MQTT_EVENT_CONNECTED handler");
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_DISCONNECTED");
            rc = xSemaphoreTake(xMQTTClientMutex, portMAX_DELAY);
            if (pdPASS != rc) {
                ESP_LOGW(MQTT_TAG, "Failed tp get MQTT Mutex MQTT_EVENT_DISCONNECTED handler");
            }
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(MQTT_TAG, "Unhandled event received %d", event->event_id);
    }
    return ESP_OK;
}

static void mqtt_app_start(void)
{
    esp_err_t rc;
    const esp_mqtt_client_config_t mqtt_cfg = {
        .client_id = CONFIG_ESP_MQTT_CLIENTID,
        .uri = CONFIG_ESP_MQTT_BROKER_URI,
// If using test vagrant, enter IP address of host laptop, comment out the .host entry
// if using broker with DNS resolvable address     
        .host = "192.168.0.32",
        .username = CONFIG_ESP_MQTT_BROKER_USERNAME,
        .password = CONFIG_ESP_MQTT_BROKER_USERPWD,
// uncomment out line below to enable server cert validation
//        .cert_pem = (const char *)m2mqtt_ca_pem_start,
        .event_handle = mqtt_event_handler
    };

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    client = esp_mqtt_client_init(&mqtt_cfg);
    rc = esp_mqtt_client_start(client);
    if (ESP_OK != rc) {
        ESP_LOGE(MQTT_TAG, "Failed to start ESP client rc = %i", rc);
    }
}

void app_main()
{   
    BaseType_t rc;
    static httpd_handle_t server = NULL;
    esp_log_level_set("*", ESP_LOG_INFO);
    // esp_log_level_set(TAG, ESP_LOG_DEBUG);
    ESP_LOGI(TAG, "ESP32 water monitor!\n");

    xMQTTClientMutex = xSemaphoreCreateBinary();
    if (NULL == xMQTTClientMutex) {
        ESP_LOGE(TAG, "Failed to create xMQTTClientMutex");
        esp_restart();
    }
    //rc = xSemaphoreTake(xMQTTClientMutex, 0);
    //if (pdPASS != rc) {
    //    ESP_LOGW(TAG, "Failed to take xMQTTClientMutex");
    //    esp_restart();
    //}
    
    // connect to the wifi network
    wifiStart(&server);
    // start the MQTT client
    ESP_LOGI(TAG, "Connecting to the MQTT server... ");
    mqtt_app_start();
    //create queues and start tasks to manage waterflow monitoring
    flow_init();
}
