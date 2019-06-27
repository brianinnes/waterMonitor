#include "freertos/event_groups.h"
#include <mqtt_client.h>

extern const char *MQTT_TAG;
extern const char TAG[];
extern esp_mqtt_client_handle_t client;
extern SemaphoreHandle_t xMQTTClientMutex;
extern const int CONNECTED_BIT;
extern EventGroupHandle_t wifi_event_group;

extern void flow_init();
extern void wifiStart();
extern void stop_WebServer();
extern void start_WebServer();