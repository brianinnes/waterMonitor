#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <sys/time.h>
#include "driver/adc.h"
#include <cJSON.h>
#include <mqtt_client.h>
#include "esp_system.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "waterMonitor.h"
#include "esp_adc_cal.h"

// Flow meter handler
long litreClicks = 0L;
long protectedCurrPulses = 0L;
TickType_t delay_1Sec = 1000/portTICK_PERIOD_MS;
TickType_t delay_100ms = 100/portTICK_PERIOD_MS;

#define FLOW_GPIO (27)
#define PRE_TDS_GPIO (25)
#define PRE_TDS_ADC (33)
#define POST_TDS_GPIO (26)
#define POST_TDS_ADC (34)
#define DEFAULT_VREF  1100
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<PRE_TDS_GPIO) | (1ULL<<POST_TDS_GPIO))

static esp_adc_cal_characteristics_t *adc_chars;
adc1_channel_t pre, post;
static QueueHandle_t qPulse, qStoppedFlow, qReportFlow, qReportQuality /*, qPersist*/;
SemaphoreHandle_t sCountSem;
bool flowing = false;

typedef struct {
    struct timeval tme;
    float litres;
    float rate;
} reportFlowQ_t;

typedef struct {
    int preFilter;
    int postFilter;
} reportQualityQ_t;

inline long getCurrentPulses() {
    long ret = 0L;
    xSemaphoreTake(sCountSem, portMAX_DELAY);
    ret = protectedCurrPulses;
    xSemaphoreGive(sCountSem);
    return ret;
}

inline void setCurrentPulses(long p) {
    xSemaphoreTake(sCountSem, portMAX_DELAY);
    protectedCurrPulses = p;
    xSemaphoreGive(sCountSem);
}

static void handler(void *args) {
    litreClicks++;
	xQueueSendToBackFromISR(qPulse, &litreClicks, ( TickType_t )0);
}

adc1_channel_t adc1PinToChannel(uint8_t pin) {
    adc1_channel_t ret = 0;
    switch(pin) {
        case ADC1_CHANNEL_0_GPIO_NUM :
            ret = ADC1_CHANNEL_0;
            break;
        case ADC1_CHANNEL_1_GPIO_NUM :
            ret = ADC1_CHANNEL_1;
            break;
        case ADC1_CHANNEL_2_GPIO_NUM :
            ret = ADC1_CHANNEL_2;
            break;
        case ADC1_CHANNEL_3_GPIO_NUM :
            ret = ADC1_CHANNEL_3;
            break;
        case ADC1_CHANNEL_4_GPIO_NUM :
            ret = ADC1_CHANNEL_4;
            break;
        case ADC1_CHANNEL_5_GPIO_NUM :
            ret = ADC1_CHANNEL_5;
            break;
        case ADC1_CHANNEL_6_GPIO_NUM :
            ret = ADC1_CHANNEL_6;
            break;
        case ADC1_CHANNEL_7_GPIO_NUM :
            ret = ADC1_CHANNEL_7;
            break;
        default :
            break;
    }
    return ret;
}

int readTDS(uint8_t powerPin, adc1_channel_t analogChannel, long Pulses) {
    ESP_LOGI(TAG, "Reading TDS: power pin=%i, ADC channel=%i", powerPin, analogChannel);
    int ret = -1;
    struct timeval now;
    long steadyTime;

    gpio_set_level(powerPin, 1);
    // wait 10 seconds for reading to settle down
    gettimeofday(&now, 0);
    steadyTime = now.tv_sec + 10;
    while (steadyTime > now.tv_sec ) {
        vTaskDelay( delay_1Sec );
        if (Pulses != getCurrentPulses()) {
            break;
        }
        gettimeofday(&now, 0);
    }
    // take 10 reading then return average
    int adc = 0;
    for (int i = 0; i < 20; i++) {
        adc = adc + adc1_get_raw(analogChannel);
            ESP_LOGD(TAG, "Accumulative analog read = %i", adc);
        vTaskDelay(delay_100ms);
    }
    ret = adc / 20;
    gpio_set_level(powerPin, 0);
    return ret;
}

void vTaskQuality ( void *pvParameters )
{
    long pulses;
    reportQualityQ_t report;
    ESP_LOGI(TAG, "Starting Quality_task");
    while(1) {
        ESP_LOGI(TAG, "Waiting on flow stopped queue");
		xQueueReceive(qStoppedFlow, &pulses, portMAX_DELAY);
        ESP_LOGD(TAG, "received message on stopped flow queue");
        report.preFilter = readTDS(PRE_TDS_GPIO, pre, pulses);
        if ((0 <= report.preFilter) && (pulses == getCurrentPulses())) {
            report.postFilter = readTDS(POST_TDS_GPIO, post, pulses);
            if ((0 <= report.postFilter) && (pulses == getCurrentPulses())) {
                ESP_LOGD(TAG, "sending message on report quality queue");
                xQueueSendToBack(qReportQuality, &report, ( TickType_t )0);
            }
        }
    }
    vTaskDelete( NULL );    
}

void vTaskStats( void *pvParameters )
{
    long pulses, previousReportingPulses;
    float previousReportingRate;
    struct timeval previousReporting, now;
    reportFlowQ_t report;

	ESP_LOGI(TAG, "Starting flow_task");
    gettimeofday(&previousReporting, 0);
    long interval = 0L;
    pulses = getCurrentPulses();
    previousReportingRate = 0.0;
    previousReportingPulses = pulses;
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
        gettimeofday(&now, 0);
        if (errQUEUE_EMPTY == rc) {
            // no queue item received - 1 second timeout
            ESP_LOGD(TAG, "Timeout waiting for pulse, pulse count = %li", pulses);
        } else {
            setCurrentPulses(pulses);
        }
        interval = (now.tv_sec-previousReporting.tv_sec)*1000000 + now.tv_usec-previousReporting.tv_usec;
        ESP_LOGV(TAG, "Interval = %li", interval);
        if (pulses == previousReportingPulses) {
            if(true == flowing) {
                ESP_LOGI(TAG, "Flow stopped");
                xQueueSendToBack(qStoppedFlow, &pulses, ( TickType_t )0);
                flowing = false;
            }
            // no water has been used
            if ((interval > 30000000L) || (previousReportingRate > 0.01)) {
                //30 second reporting when idle
                report.tme.tv_sec = now.tv_sec;
                report.tme.tv_usec = now.tv_usec;
                report.litres = (float)pulses/450;
                report.rate = 0.0;
                ESP_LOGI(TAG, "Sending reporting data");
                xQueueSendToBack(qReportFlow, &report, ( TickType_t )0);
                previousReporting.tv_sec = now.tv_sec;
                previousReporting.tv_usec = now.tv_usec;
                previousReportingPulses = pulses;
                previousReportingRate = 0.0;
            }
        } else {
            flowing = true;
            if (interval > 1000000L) {
                //water being used - 1 second reporting
                report.tme.tv_sec = now.tv_sec;
                report.tme.tv_usec = now.tv_usec;
                report.litres = (float)pulses/450;
                report.rate = ((float)(pulses-previousReportingPulses)/450) * 60 / (interval / 1000000);
                xQueueSendToBack(qReportFlow, &report, ( TickType_t )0);
                previousReporting.tv_sec = now.tv_sec;
                previousReporting.tv_usec = now.tv_usec;
                previousReportingPulses = pulses;
                previousReportingRate = report.rate;
            }
        }
		ESP_LOGD(TAG, "Woke from interrupt queue wait: %i", rc);
    }
    vTaskDelete( NULL );
}

void vTaskReportFlow( void *pvParameters )
{
    reportFlowQ_t report;
    int msg_id;
    while(1) {
        //TODO Add Error checking!!!!
        ESP_LOGI(TAG, "Waiting on reporting Flow queue");
		/*BaseType_t rc = */ xQueueReceive(qReportFlow, &report, portMAX_DELAY);
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

void vTaskReportQuality( void *pvParameters )
{
    reportQualityQ_t report;
    int msg_id;
    while(1) {
        //TODO Add Error checking!!!!
        ESP_LOGI(TAG, "Waiting on reporting Quality queue");
		/*BaseType_t rc = */ xQueueReceive(qReportQuality, &report, portMAX_DELAY);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "device", CONFIG_ESP_MQTT_CLIENTID);
        cJSON *data = cJSON_CreateObject();
        cJSON_AddNumberToObject(data, "pre", report.preFilter);
        cJSON_AddNumberToObject(data, "post", report.postFilter);
        cJSON_AddItemToObject(json, "d", data);

        char *buffer = cJSON_PrintUnformatted(json);
        cJSON_Delete(json);
        ESP_LOGI(TAG, "Reporting : %s", buffer);

        xSemaphoreTake(xMQTTClientMutex, portMAX_DELAY);
        msg_id = esp_mqtt_client_publish(client, "/water/quality", buffer, strlen(buffer), 0, 0);
        xSemaphoreGive(xMQTTClientMutex);
        ESP_LOGI(MQTT_TAG, "sent publish successful, msg_id=%d", msg_id);
        free(buffer);
    }
    vTaskDelete( NULL );
}

void flow_init() {
    // setup adc
    pre = adc1PinToChannel(PRE_TDS_ADC);
    post = adc1PinToChannel(POST_TDS_ADC);
    adc_power_on();
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(pre, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(post, ADC_ATTEN_DB_11);
    
    //Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);

    // setup GPIO
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);
    gpio_set_level(PRE_TDS_GPIO, 0);
    gpio_set_level(POST_TDS_GPIO, 0);

    //create queues and start tasks to manage waterflow monitoring
    sCountSem = xSemaphoreCreateBinary();
    if (NULL != sCountSem) {
        // semaphore created OK
        protectedCurrPulses = 0;
        xSemaphoreGive(sCountSem);
    
        qPulse = xQueueCreate(10, sizeof(long));
        qStoppedFlow = xQueueCreate(5, sizeof(long));
        qReportFlow = xQueueCreate(5, sizeof(reportFlowQ_t));
        qReportQuality = xQueueCreate(5, sizeof(reportQualityQ_t));
        xTaskCreate( vTaskStats, "Stats collector", 4096, NULL, 6, NULL );
        xTaskCreate( vTaskQuality, "Quality collector", 2048, NULL, 6, NULL);
        xTaskCreate( vTaskReportFlow, "Report flow", 4096, NULL, 5, NULL );
        xTaskCreate( vTaskReportQuality, "Report quality", 4096, NULL, 5, NULL);
        // xTaskCreate( vTaskPersist, "Persist", 2000, NULL, 4, NULL ); //TODO Implement
    }
}
