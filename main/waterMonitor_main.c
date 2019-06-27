#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>
#include <nvs_flash.h>
#include <sys/time.h>
#include <mdns.h>
#include <esp_http_server.h>
#include <cJSON.h>
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "waterMonitor.h"


const char *MQTT_TAG = "waterMonitor_mqtt";
const char TAG[] = "waterMonitor";
esp_mqtt_client_handle_t client;
SemaphoreHandle_t xMQTTClientMutex;
const int CONNECTED_BIT = BIT0;
EventGroupHandle_t wifi_event_group;

// OTA config structure
typedef struct {
    char url[64];
    char cert[1500];
} otaConfig;
otaConfig otac;

// MQTT client configuration
extern const uint8_t m2mqtt_ca_pem_start[] asm("_binary_m2mqtt_ca_pem_start");
extern const uint8_t m2mqtt_ca_pem_end[]   asm("_binary_m2mqtt_ca_pem_end");

static void mqtt_app_start(void);

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

void simple_ota_task(void * pvParameter)
{   
    esp_http_client_config_t config = {
        .url = otac.url,
        .cert_pem = otac.cert,
        .event_handler = _http_event_handler,
    };
    esp_err_t ret = esp_https_ota(&config);
    if (ret == ESP_OK) {
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Firmware upgrade failed");
    }
    
    vTaskDelete( NULL );
}

static void handleIncomingEvent(esp_mqtt_event_handle_t event) {
    ESP_LOGD(MQTT_TAG,"Event received : TOPIC=%.*s\r\n", event->topic_len, event->topic);
    ESP_LOGD(MQTT_TAG,"Event received : DATA=%.*s\r\n", event->data_len, event->data);
    if (strncmp(event->topic, "/ota/release", event->topic_len) == 0) 
    {
        ESP_LOGI(MQTT_TAG, "OTA Event received");
        // kick off OTA update
        const cJSON *msg = cJSON_Parse(event->data);
        cJSON *url = cJSON_GetObjectItemCaseSensitive(msg, "URL");
        cJSON *cert = cJSON_GetObjectItemCaseSensitive(msg, "cert");
        strcpy(otac.url, url->valuestring);
        strcpy(otac.cert, cert->valuestring);
        cJSON_Delete((cJSON *)msg);
        printf("OTA from %s, cert=%s\r\n", otac.url, otac.cert);
        xTaskCreate(&simple_ota_task, "ota_example_task", 8192, NULL, 5, NULL);
    } 
    else /* default: */
    {
        ;
    } 
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    BaseType_t rc;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_CONNECTED");
            esp_mqtt_client_subscribe(client, "/ota/release", 0);
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
            handleIncomingEvent(event);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_ERROR");
            break;
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_BEFORE_CONNECT");
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
        .buffer_size = 1536,
// If using test vagrant, enter IP address of host laptop, comment out the .host entry
// if using broker with DNS resolvable address     
//        .host = "192.168.0.32",
        .username = CONFIG_ESP_MQTT_BROKER_USERNAME,
//        .password = CONFIG_ESP_MQTT_BROKER_USERPWD,
// uncomment out line below to enable server cert validation
        .cert_pem = (const char *)m2mqtt_ca_pem_start,
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
    ESP_LOGI(TAG, "ESP32 water monitor!\n");
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[14], PIN_FUNC_GPIO);
    
    xMQTTClientMutex = xSemaphoreCreateBinary();
    if (NULL == xMQTTClientMutex) {
        ESP_LOGE(TAG, "Failed to create xMQTTClientMutex");
        esp_restart();
    }

    /* Initialize NVS. */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    // connect to the wifi network
    wifiStart(&server);
    
    // wait for WiFi to connect and get an IP address
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);
                        
    // give time for NTP to get correct time so cert dates can be checked                    
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    // start the MQTT client
    ESP_LOGI(TAG, "Connecting to the MQTT server... ");
    mqtt_app_start();
    //create queues and start tasks to manage waterflow monitoring
    flow_init();
}
