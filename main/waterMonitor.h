#include <mqtt_client.h>

extern const char *MQTT_TAG;
extern const char TAG[];
extern esp_mqtt_client_handle_t client;
extern SemaphoreHandle_t xMQTTClientMutex;

extern void flow_init();
extern void wifiStart();