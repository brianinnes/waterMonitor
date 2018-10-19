#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>
#include <lwip/apps/sntp.h>
#include <sys/time.h>
#include <mqtt_client.h>
#include <mdns.h>
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "waterMonitor.h"

const char *MQTT_TAG = "waterMonitor_mqtt";
const char TAG[] = "waterMonitor";
esp_mqtt_client_handle_t client;
SemaphoreHandle_t xMQTTClientMutex;


// Event group
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

// Wifi event handler
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {

    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;

	case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        // get correct time with SNTP
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "0.uk.pool.ntp.org");
        sntp_setservername(1, "chronos.csr.net");
        sntp_init();
        break;

	case SYSTEM_EVENT_STA_DISCONNECTED:
		xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;

	default:
        break;
    }

	return ESP_OK;
}

static void wifiStart() {
    wifi_event_group = xEventGroupCreate();
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
    wifi_country_t country = {
        .cc="GB",
        .schan=1,
        .nchan=13, 
        .policy=WIFI_COUNTRY_POLICY_AUTO
    };
    esp_wifi_set_country(&country);
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "waiting for wifi network...");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
}

// MQTT client configuration
extern const uint8_t m2mqtt_ca_pem_start[] asm("_binary_m2mqtt_ca_pem_start");
extern const uint8_t m2mqtt_ca_pem_end[]   asm("_binary_m2mqtt_ca_pem_end");

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    //esp_mqtt_client_handle_t client = event->client;
    //int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_CONNECTED");
            xSemaphoreGive(xMQTTClientMutex);
            // msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
            // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(MQTT_TAG, "MQTT_EVENT_DISCONNECTED");
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
    esp_mqtt_client_start(client);
}

void app_main()
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_LOGI(TAG, "ESP32 water monitor!\n");

    xMQTTClientMutex = xSemaphoreCreateMutex();
    xSemaphoreTake(xMQTTClientMutex, portMAX_DELAY);

    // connect to the wifi network
    nvs_flash_init();
    wifiStart();

    //create queues and start tasks to manage waterflow monitoring
    flow_init();

    // start the MQTT client
    ESP_LOGI(TAG, "Connecting to the MQTT server... ");
    mqtt_app_start();
}
