#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "device_diagnostics.h"

static const char *TAG = "device_diagnostics";

/**
 * @brief Diagnostics task stack size, publishing keeps a message on the stack
 *
 */
#define DEVICE_DIAGNOSTICS_TASK_STACK_SIZE (3072 + MULTICAST_PUBSUB_MAX_MESSAGE_SIZE)

#define DEVICE_DIAGNOSTICS_TASK_PRIORITY (1)

typedef struct {
    multicast_pubsub_handle_t *pubsub; /*!< Handle to publish with */
    multicast_pubsub_topic_t topic;    /*!< Topic of diagnostics messages, its group is owned by this context */
    uint32_t interval_s;               /*!< Seconds between messages */
    TaskHandle_t task;                 /*!< Diagnostics task handle, NULL until started */
} device_diagnostics_t;

/**
 * @brief Diagnostics of this device, there is only one
 *
 */
static device_diagnostics_t diagnostics;

/**
 * @brief Publish one diagnostics message
 *
 * @param diag Started diagnostics
 */
static void publish_diagnostics(device_diagnostics_t *diag) {
    int64_t uptime_s = esp_timer_get_time() / 1000000;

    char data[64];
    int rssi;
    if (esp_wifi_sta_get_rssi(&rssi) == ESP_OK) {
        snprintf(data, sizeof(data), "{\"rssi\":%d,\"uptime\":%" PRId64 "}", rssi, uptime_s);
    } else {
        // Not connected, so the send most likely fails too, but receivers still see it as a gap
        snprintf(data, sizeof(data), "{\"uptime\":%" PRId64 "}", uptime_s);
    }

    multicast_pubsub_publish(diag->pubsub, &diag->topic, data);
}

/**
 * @brief Device Diagnostics Task Entry
 *
 * @param arg argument
 */
static void device_diagnostics_task_entry(void *arg) {
    device_diagnostics_t *diag = (device_diagnostics_t *) arg;
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS((uint64_t) diag->interval_s * 1000));
        publish_diagnostics(diag);
    }
}

/**
 * @brief Send diagnostics messages
 *
 * @param pubsub Handle of multicast pubsub to publish with, must have a source
 * @param config Configuration of diagnostics messages, the group is copied
 * @return ESP_OK if started, or if the interval is 0
 */
esp_err_t device_diagnostics_start(multicast_pubsub_handle_t *pubsub, const device_diagnostics_config_t *config) {
    if (pubsub == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->interval_s > DEVICE_DIAGNOSTICS_MAX_INTERVAL_S) {
        ESP_LOGE(TAG, "interval must be at most %d s", DEVICE_DIAGNOSTICS_MAX_INTERVAL_S);
        return ESP_ERR_INVALID_ARG;
    }
    if (diagnostics.task != NULL) {
        ESP_LOGE(TAG, "diagnostics already started");
        return ESP_ERR_INVALID_STATE;
    }
    if (config->interval_s == 0) {
        ESP_LOGI(TAG, "Diagnostics turned off");
        return ESP_OK;
    }

    multicast_pubsub_topic_t topic = {
            .type = DEVICE_DIAGNOSTICS_TYPE,
            .group = config->group,
            .port = config->port
    };
    esp_err_t err = multicast_pubsub_check_topic(&topic);
    if (err != ESP_OK) {
        return err;
    }

    char *group = strdup(config->group);
    if (!group) {
        ESP_LOGE(TAG, "copy of group failed");
        return ESP_ERR_NO_MEM;
    }
    topic.group = group;

    diagnostics.pubsub = pubsub;
    diagnostics.topic = topic;
    diagnostics.interval_s = config->interval_s;

    BaseType_t created = xTaskCreate(
            device_diagnostics_task_entry,
            "diagnostics",
            DEVICE_DIAGNOSTICS_TASK_STACK_SIZE,
            &diagnostics,
            DEVICE_DIAGNOSTICS_TASK_PRIORITY,
            &diagnostics.task);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "create diagnostics task failed");
        memset(&diagnostics, 0, sizeof(diagnostics));
        free(group);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Sending diagnostics to %s:%u every %" PRIu32 " s", group, config->port, config->interval_s);

    return ESP_OK;
}
