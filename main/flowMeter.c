#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <sys/time.h>
#include <cJSON.h>
#include <mqtt_client.h>
#include "esp_system.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "waterMonitor.h" 

// Flow meter handler
long litreClicks = 0;
#define FLOW_GPIO (27)
static QueueHandle_t qPulse, qReport /*, qPersist*/;

typedef struct {
    struct timeval tme;
    float litres;
    float rate;
} reportQ_t;

static void handler(void *args) {
    litreClicks++;
	xQueueSendToBackFromISR(qPulse, &litreClicks, ( TickType_t )0);
}

void vTaskStats( void *pvParameters )
{
    long pulses, previousReportingPulses;
    float previousReportingRate;
    struct timeval previousReporting, now;
    reportQ_t report;

	ESP_LOGI(TAG, ">> flow_task");
    TickType_t delay_1Sec = 1000/portTICK_PERIOD_MS;
    gettimeofday(&previousReporting, 0);
    long interval = 0L;
    pulses = 0L;
    previousReportingRate = 0.0;
    previousReportingPulses = 0L;
    previousReporting.tv_sec = 0;
    previousReporting.tv_usec = 0;

	gpio_config_t gpioConfig;
	gpioConfig.pin_bit_mask = GPIO_SEL_27;
	gpioConfig.mode         = GPIO_MODE_INPUT;
	gpioConfig.pull_up_en   = GPIO_PULLUP_DISABLE;
	gpioConfig.pull_down_en = GPIO_PULLUP_DISABLE;
	gpioConfig.intr_type    = GPIO_INTR_POSEDGE;
	gpio_config(&gpioConfig);

	gpio_install_isr_service(0);
	gpio_isr_handler_add(FLOW_GPIO, handler, NULL	);

    while(1) {
        ESP_LOGV(TAG, "Waiting on interrupt queue");
		BaseType_t rc = xQueueReceive(qPulse, &pulses, delay_1Sec);
        if (errQUEUE_EMPTY == rc) {
            // no queue item received - 1 second timeout
            ESP_LOGD(TAG, "Timeout waiting for pulse, pulse count = %li", pulses);
        }
        gettimeofday(&now, 0);
        interval = (now.tv_sec-previousReporting.tv_sec)*1000000 + now.tv_usec-previousReporting.tv_usec;
        ESP_LOGV(TAG, "Interval = %li", interval);
        if (pulses == previousReportingPulses) {
            // no water has been used
            if ((interval > 30000000L) || (previousReportingRate > 0.01)) {
                //30 second reporting when idle
                report.tme.tv_sec = now.tv_sec;
                report.tme.tv_usec = now.tv_usec;
                report.litres = (float)pulses/450;
                report.rate = 0.0;
                ESP_LOGI(TAG, "Sending reporting data");
                xQueueSendToBack(qReport, &report, ( TickType_t )0);
                previousReporting.tv_sec = now.tv_sec;
                previousReporting.tv_usec = now.tv_usec;
                previousReportingPulses = pulses;
                previousReportingRate = 0.0;
            }
        } else if (interval > 1000000L) {
            //water being used - 1 second reporting
            report.tme.tv_sec = now.tv_sec;
            report.tme.tv_usec = now.tv_usec;
            report.litres = (float)pulses/450;
            report.rate = ((float)(pulses-previousReportingPulses)/450) * 60 / (interval / 1000000);
            xQueueSendToBack(qReport, &report, ( TickType_t )0);
            previousReporting.tv_sec = now.tv_sec;
            previousReporting.tv_usec = now.tv_usec;
            previousReportingPulses = pulses;
            previousReportingRate = report.rate;
        }
		ESP_LOGD(TAG, "Woke from interrupt queue wait: %i", rc);
    }
    vTaskDelete( NULL );
}

void vTaskReport( void *pvParameters )
{
    reportQ_t report;
    int msg_id;
    while(1) {
        //TODO Add Error checking!!!!
        ESP_LOGV(TAG, "Waiting on reporting queue");
		/*BaseType_t rc = */ xQueueReceive(qReport, &report, portMAX_DELAY);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "device", CONFIG_ESP_MQTT_CLIENTID);
        cJSON *data = cJSON_CreateObject();
        cJSON_AddNumberToObject(data, "litres", report.litres);
        cJSON_AddNumberToObject(data, "rate", report.rate);
        cJSON_AddItemToObject(json, "d", data);

        char *buffer = cJSON_PrintUnformatted(json);
        cJSON_Delete(json);
        ESP_LOGI(TAG, "Reporting : %s", buffer);

        xSemaphoreTake(xMQTTClientMutex, portMAX_DELAY);
        msg_id = esp_mqtt_client_publish(client, "/water/flow", buffer, strlen(buffer), 0, 0);
        xSemaphoreGive(xMQTTClientMutex);
        ESP_LOGI(MQTT_TAG, "sent publish successful, msg_id=%d", msg_id);
        free(buffer);
    }
    vTaskDelete( NULL );
}

/*
void vTaskPersist( void *pvParameters )
{
    while(1) {
    }
    vTaskDelete( NULL );
}
*/

void flow_init() {
    //create queues and start tasks to manage waterflow monitoring
    qPulse = xQueueCreate(10, sizeof(long));
    qReport = xQueueCreate(10, sizeof(reportQ_t));
    xTaskCreate( vTaskStats, "Stats collector", 2048, NULL, 6, NULL );
    xTaskCreate( vTaskReport, "Reporting", 4096, NULL, 5, NULL );
    // xTaskCreate( vTaskPersist, "Persist", 2000, NULL, 4, NULL ); //TODO Implement
}
