#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <lwip/apps/sntp.h>
#include <esp_log.h>
#include <esp_event_loop.h>
#include "waterMonitor.h"


#define WM_MDNS_INSTANCE CONFIG_MDNS_INSTANCE

const char TAGWIFI[] = "waterMonitorWiFi";
//const char TAGHTTPD[] = "waterMonitorHttpd";
static const char c_config_hostname[] = CONFIG_MDNS_HOSTNAME;


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
    
    case SYSTEM_EVENT_AP_START:
         // Start the web server
        start_WebServer();
        break;

    case SYSTEM_EVENT_AP_STOP:
        stop_WebServer();
        break;

	default:
        ESP_LOGI(TAGWIFI, "Event %i received", event->event_id);
        break;
    }

	return ESP_OK;
}

void wifiStart(void *arg) {
    wifi_event_group = xEventGroupCreate();
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, arg));
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
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
 
    const size_t config_hostname_len = sizeof(c_config_hostname) - 1; // without term char
    char hostname[config_hostname_len + 1 + 3*2 + 1]; // adding underscore + 3 digits + term char
    uint8_t mac[6];

    // adding 3 LSBs from mac addr to setup a board specific name
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(hostname, sizeof(hostname), "%s_%02x%02X%02X", c_config_hostname, mac[3], mac[4], mac[5]);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    wifi_config_t apConfig = {
    .ap = {
        .ssid="xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
        .ssid_len=0,
        .password="passw0rd",
        .channel=0,
        .authmode=WIFI_AUTH_WPA_WPA2_PSK,
        .ssid_hidden=0,
        .max_connection=4,
        .beacon_interval=100
        } 
    };
    snprintf((char *)apConfig.ap.ssid, sizeof(hostname), "%s", hostname);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &apConfig));
    
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "waiting for wifi network...");
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
    
}
