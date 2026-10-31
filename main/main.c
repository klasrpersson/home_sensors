#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "vindriktning_parser.h"
#include "wlan_station.h"
#include "multicast_pubsub.h"

static const char *TAG = "Vindriktning";

typedef struct {
    vindriktning_parser_handle_t *parser;
    multicast_pubsub_handle_t *publisher;
} main_task_context_t;

static multicast_pubsub_topic_t air_quality_topic = {
    .type = "air_quality",
    .group = CONFIG_MULTICAST_GROUP,
    .port = CONFIG_MULTICAST_PORT
};

_Noreturn static void main_task_entry(void *args) {
    main_task_context_t *context = (main_task_context_t *) args;
    
    vindriktning_read_event_t event;
    for (;;)
    {
        if (xQueueReceive(context->parser->event_queue, &event, portMAX_DELAY))
        {
            ESP_LOGI(TAG, "AVG PM1=%u, PM10=%u, PM25=%u", event.pm1, event.pm10, event.pm25);

            if (context->publisher != NULL)
            {
                char data[64];
                snprintf(data, sizeof(data), "{\"pm1\":%u,\"pm10\":%u,\"pm25\":%u}", event.pm1, event.pm10, event.pm25);
                multicast_pubsub_publish(context->publisher, &air_quality_topic, data);
            }
        }
    }
}

void app_main(void)
{
    static main_task_context_t context;

    vindriktning_parser_config_t parser_config = VINDRIKTNING_PARSER_CONFIG_DEFAULT();
    context.parser = vindriktning_parser_init(&parser_config);
    if (context.parser == NULL) {
        ESP_LOGE(TAG, "Failed to init Vindriktning parser");
        return;
    }

    wlan_station_config_t wifi_config = {
        .ssid = CONFIG_WIFI_SSID,
        .password = CONFIG_WIFI_PASSWORD
    };

    // Check the multicast settings now, instead of failing on every measurement
    if (multicast_pubsub_check_topic(&air_quality_topic) != ESP_OK) {
        ESP_LOGE(TAG, "Invalid multicast group, measurements will not be published");
    } else if (wlan_station_start(&wifi_config) == ESP_OK) {
        multicast_pubsub_config_t publisher_config = {
            .source = CONFIG_MULTICAST_SOURCE,
            .ttl = CONFIG_MULTICAST_TTL
        };
        context.publisher = multicast_pubsub_init(&publisher_config);
    } else {
        ESP_LOGE(TAG, "Failed to start Wi-Fi, measurements will not be published");
    }

    ESP_LOGI(TAG, "Starting main task");
    
    BaseType_t err = xTaskCreate(main_task_entry, "main_task", 4096, &context, uxTaskPriorityGet(NULL), NULL);

    if (err != pdPASS) {
        ESP_LOGE(TAG, "Failed to create main task");
    } else {
        ESP_LOGI(TAG, "Created main task");
    }   
}
