#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <lwip/apps/sntp.h>
#include <esp_log.h>
#include <esp_event_loop.h>
#include <esp_http_server.h>
#include "waterMonitor.h"
#include "captdns.h"
#include "mdns.h"

#define WM_MDNS_INSTANCE CONFIG_MDNS_INSTANCE

const char TAGWIFI[] = "waterMonitorWiFi";
const char TAGHPPTD[] = "waterMonitorHttpd";
static const char c_config_hostname[] = CONFIG_MDNS_HOSTNAME;

// ******************** Web Server

/* An HTTP GET handler */
esp_err_t hello_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    ESP_LOGI(TAGHPPTD, "hello handler called");
    buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        // Copy null terminated value string into buffer
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAGHPPTD, "Found header => Host: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-2") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Test-Header-2", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAGHPPTD, "Found header => Test-Header-2: %s", buf);
        }
        free(buf);
    }

    buf_len = httpd_req_get_hdr_value_len(req, "Test-Header-1") + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_hdr_value_str(req, "Test-Header-1", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAGHPPTD, "Found header => Test-Header-1: %s", buf);
        }
        free(buf);
    }

    // Read URL query string length and allocate memory for length + 1,
    // extra byte for null termination
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        buf = malloc(buf_len);
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAGHPPTD, "Found URL query => %s", buf);
            char param[32];
            // Get value of expected key from query string
            if (httpd_query_key_value(buf, "query1", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAGHPPTD, "Found URL query parameter => query1=%s", param);
            }
            if (httpd_query_key_value(buf, "query3", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAGHPPTD, "Found URL query parameter => query3=%s", param);
            }
            if (httpd_query_key_value(buf, "query2", param, sizeof(param)) == ESP_OK) {
                ESP_LOGI(TAGHPPTD, "Found URL query parameter => query2=%s", param);
            }
        }
        free(buf);
    }

    // Set some custom headers
    httpd_resp_set_hdr(req, "Custom-Header-1", "Custom-Value-1");
    httpd_resp_set_hdr(req, "Custom-Header-2", "Custom-Value-2");

    // Send response with custom headers and body set as the
    // string passed in user context
    const char* resp_str = (const char*) req->user_ctx;
    httpd_resp_send(req, resp_str, strlen(resp_str));

    // After sending the HTTP response the old HTTP request
    // headers are lost. Check if HTTP request headers can be read now.
    if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
        ESP_LOGI(TAGHPPTD, "Request headers lost");
    }
    return ESP_OK;
}

// An HTTP GET handler
esp_err_t redirect_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAGHPPTD, "redirect handler called");
    httpd_resp_set_hdr(req, "location", (const char*) req->user_ctx);
//    httpd_resp_set_status(req, HTTPD_302);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

httpd_uri_t hello = {
    .uri       = "/hello",
    .method    = HTTP_GET,
    .handler   = hello_get_handler,
    .user_ctx  = "<h1>ESP32 Water Monitor</h1>"
};

httpd_uri_t redirect = {
    .uri       = "",
    .method    = HTTP_GET,
    .handler   = redirect_get_handler,
    .user_ctx  = "http://192.168.4.1/hello"
};

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_open_sockets = 16;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAGHPPTD, "Registering URI handlers");
//        httpd_register_uri_handler(server, &hello);
//        httpd_register_404_handler(server, &redirect);
        return server;
    }

    ESP_LOGI(TAGHPPTD, "Error starting server!");
    return NULL;
}

void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}

// ******************** WiFi


// Event group
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;


// Wifi event handler
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    httpd_handle_t *server = (httpd_handle_t *) ctx;
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
        if (*server == NULL) {
            *server = start_webserver();
        }
        break;

    case SYSTEM_EVENT_AP_STOP:
        if (*server) {
            stop_webserver(*server);
            *server = NULL;
        }
        break;

	default:
        ESP_LOGI(TAGWIFI, "Event %i received", event->event_id);
        break;
    }

	return ESP_OK;
}

static void initialise_mdns(char *hostname)
{
    //initialize mDNS
    ESP_ERROR_CHECK( mdns_init() );
    //set mDNS hostname (required if you want to advertise services)
    ESP_ERROR_CHECK( mdns_hostname_set(hostname) );
    ESP_LOGI(TAG, "mdns hostname set to: [%s]", hostname);
    //set default mDNS instance name
    ESP_ERROR_CHECK( mdns_instance_name_set(WM_MDNS_INSTANCE) );

    //structure with TXT records
    mdns_txt_item_t serviceTxtData[3] = {
        {"board","esp32"},
        {"u","user"},
        {"p","password"}
    };

    //initialize service
    ESP_ERROR_CHECK( mdns_service_add(hostname, "_http", "_tcp", 80, serviceTxtData, 3) );
    //change TXT item value
    ESP_ERROR_CHECK( mdns_service_txt_item_set("_http", "_tcp", "u", "admin") );
}

void wifiStart(void *arg) {
    nvs_flash_init();
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

    _Static_assert(sizeof(c_config_hostname) < CONFIG_MAIN_TASK_STACK_SIZE/2, "Configured mDNS name consumes more than half of the stack. Please select a shorter host name or extend the main stack size please.");
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
    captdnsInit();
    initialise_mdns(hostname);
    
}
