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
#include "esp_spiffs.h"

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
#define PERSIST_DIFFERENCE 500
#define PERSIST_PERIOD 900

static esp_adc_cal_characteristics_t *adc_chars;
static adc1_channel_t pre, post;
static QueueHandle_t qPulse, qStoppedFlow, qPersistPulses, qReportFlow, qReportQuality /*, qPersist*/;
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
    long pulses, previousReportingPulses, previousReportingTime;
    float previousReportingRate;
    struct timeval now;
    reportFlowQ_t report;

	ESP_LOGI(TAG, "Starting flow_task");
    long interval = 0L;
    pulses = getCurrentPulses();
    previousReportingRate = 0.0;
    previousReportingPulses = pulses;
    previousReportingTime = 0;

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
            xQueueSendToBack(qPersistPulses, &pulses, ( TickType_t )0);
        }
        interval = now.tv_sec-previousReportingTime;
        if ((0 > interval) || (32 < interval)) {
            ESP_LOGW(TAG, "Interval out of bounds = %li", interval);
            interval = 0;
            previousReportingTime = now.tv_sec;
        }
        if (pulses == previousReportingPulses) {
            if(true == flowing) {
                ESP_LOGI(TAG, "Flow stopped");
                xQueueSendToBack(qStoppedFlow, &pulses, ( TickType_t )0);
                flowing = false;
            }
            // no water has been used
            if ((interval > 30L) || (previousReportingRate > 0.01)) {
                //30 second reporting when idle
                report.tme.tv_sec = now.tv_sec;
                report.tme.tv_usec = now.tv_usec;
                report.litres = (float)pulses/450;
                report.rate = 0.0;
                ESP_LOGI(TAG, "Sending reporting data");
                xQueueSendToBack(qReportFlow, &report, ( TickType_t )0);
                previousReportingTime = now.tv_sec;
                previousReportingPulses = pulses;
                previousReportingRate = 0.0;
                // give persist task chance to persist latest value
                xQueueSendToBack(qPersistPulses, &pulses, ( TickType_t )0);
            }
        } else {
            flowing = true;
            if (interval > 1L) {
                //water being used - 1 second reporting
                report.tme.tv_sec = now.tv_sec;
                report.tme.tv_usec = now.tv_usec;
                report.litres = (float)pulses/450;
                report.rate = ((float)(pulses-previousReportingPulses)/450) * 60 / interval;
                xQueueSendToBack(qReportFlow, &report, ( TickType_t )0);
                previousReportingTime = now.tv_sec;
                previousReportingPulses = pulses;
                previousReportingRate = report.rate;
            }
        }
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

void persistPulses(long pulses) {
    FILE* f = fopen("/fs/pulses.txt", "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fwrite(&pulses, sizeof pulses, 1, f);
    fclose(f);
    ESP_LOGI(TAG, "Pulse file written");    
}

long readPulses() {
    long ret;
    FILE* f = fopen("/fs/pulses.txt", "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return 0L;
    }
    fread(&ret, sizeof ret, 1, f);
    fclose(f);
    ESP_LOGI(TAG, "Pulse file read = %li", ret);
    return ret;
}

void vTaskPersist( void *pvParameters )
{
    long pulses, nextPersisPulses, nextPersistTime, lastPersistPulses;
    struct timeval now;
    ESP_LOGI(TAG, "Starting persist task");

    gettimeofday(&now, 0);
    nextPersistTime = now.tv_sec + PERSIST_PERIOD;
    lastPersistPulses = getCurrentPulses();
    nextPersisPulses = lastPersistPulses + PERSIST_DIFFERENCE;

    while(1) {
        ESP_LOGD(TAG, "Waiting on persit Pulses queue");
		xQueueReceive(qPersistPulses, &pulses, portMAX_DELAY);
        gettimeofday(&now, 0);
        // don't persist too often 
        // (big change in pulse count or a period of time since last persist)
        if ((now.tv_sec > nextPersistTime) || (pulses > nextPersisPulses)) {
            // don't persist if value hasn't changed
            if (lastPersistPulses != pulses) {
                persistPulses(pulses);
                lastPersistPulses = pulses;
                nextPersisPulses = lastPersistPulses + PERSIST_DIFFERENCE;
                nextPersistTime = now.tv_sec + PERSIST_PERIOD;
            }
        }
    }
    esp_vfs_spiffs_unregister(NULL);
    ESP_LOGI(TAG, "SPIFFS unmounted");
    vTaskDelete( NULL );
}

void flow_init() {
    BaseType_t rc;
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
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    gpio_set_level(PRE_TDS_GPIO, 0);
    gpio_set_level(POST_TDS_GPIO, 0);

    ESP_LOGI(TAG, "Initializing SPIFFS");
    
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/fs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
    }

    //create queues and start tasks to manage waterflow monitoring
    sCountSem = xSemaphoreCreateBinary();
    if (NULL != sCountSem) {
        // semaphore created OK
        protectedCurrPulses = readPulses();
        litreClicks = protectedCurrPulses;
        xSemaphoreGive(sCountSem);
    
        qPulse = xQueueCreate(10, sizeof(long));
        if (NULL == qPulse) {
            ESP_LOGE(TAG, "Failed to create qPulse queue");
            esp_restart();
        }
        qStoppedFlow = xQueueCreate(5, sizeof(long));
        if (NULL == qStoppedFlow) {
            ESP_LOGE(TAG, "Failed to create qStoppedFlow queue");
            esp_restart();
        }
        qPersistPulses = xQueueCreate(5, sizeof(long));
        if (NULL == qPersistPulses) {
            ESP_LOGE(TAG, "Failed to create qPersistPulses queue");
            esp_restart();
        }
        qReportFlow = xQueueCreate(5, sizeof(reportFlowQ_t));
        if (NULL == qReportFlow) {
            ESP_LOGE(TAG, "Failed to create qReportFlow queue");
            esp_restart();
        }
        qReportQuality = xQueueCreate(5, sizeof(reportQualityQ_t));
        if (NULL == qReportQuality) {
            ESP_LOGE(TAG, "Failed to create qReportQuality queue");
            esp_restart();
        }
        rc = xTaskCreate( vTaskStats, "Stats collector", 4096, NULL, 6, NULL );
        if (pdPASS != rc) {
            ESP_LOGE(TAG, "Failed to create Stats collector task");
            esp_restart();
        }
        rc = xTaskCreate( vTaskQuality, "Quality collector", 2048, NULL, 6, NULL);
        if (pdPASS != rc) {
            ESP_LOGE(TAG, "Failed to create Quality collector task");
            esp_restart();
        }
        rc = xTaskCreate( vTaskReportFlow, "Report flow", 4096, NULL, 5, NULL );
        if (pdPASS != rc) {
            ESP_LOGE(TAG, "Failed to create Report flow task");
            esp_restart();
        }
        rc = xTaskCreate( vTaskReportQuality, "Report quality", 4096, NULL, 5, NULL);
        if (pdPASS != rc) {
            ESP_LOGE(TAG, "Failed to create Report quality task");
            esp_restart();
        }
        rc = xTaskCreate( vTaskPersist, "Persist", 2000, NULL, 4, NULL ); //TODO Implement
        if (pdPASS != rc) {
            ESP_LOGE(TAG, "Failed to create Persist task");
            esp_restart();
        }
    }
}
