#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "cJSON.h"
#include "wlan_station.h"
#include "multicast_pubsub.h"

static const char *TAG = "Receiver";

static const multicast_pubsub_topic_t air_quality_topic = {
    .type = "air_quality",
    .group = CONFIG_MULTICAST_GROUP,
    .port = CONFIG_MULTICAST_PORT
};

static void on_message(const multicast_pubsub_message_t *message, void *ctx) {
    char *data = cJSON_PrintUnformatted(message->data);
    ESP_LOGI(TAG, "%s from %s (%s) #%" PRIu32 ": %s", message->type, message->source, message->sender, message->seq,
             data != NULL ? data : "?");
    cJSON_free(data);
}

void app_main(void)
{
    wlan_station_config_t wifi_config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
        .disable_power_save = true
    };
    if (wlan_station_start(&wifi_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi, check the network in wifi_credentials.h");
        return;
    }

    // Join the group once connected
    wlan_station_wait_connected(portMAX_DELAY);

    // No source, as the receiver only subscribes
    multicast_pubsub_config_t pubsub_config = {
        .source = NULL
    };
    multicast_pubsub_handle_t *pubsub = multicast_pubsub_init(&pubsub_config);
    if (pubsub == NULL) {
        ESP_LOGE(TAG, "Failed to init multicast");
        return;
    }

    if (multicast_pubsub_subscribe(pubsub, &air_quality_topic, on_message, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to subscribe to %s", air_quality_topic.type);
        return;
    }

    ESP_LOGI(TAG, "Waiting for %s messages", air_quality_topic.type);
}
