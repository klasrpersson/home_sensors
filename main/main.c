#include <stdio.h>
#include "freertos/queue.h"
#include "esp_log.h"
#include "vindriktning_parser.h"

static const char *TAG = "Vindriktning";

_Noreturn static void main_task_entry(void *args) {
    vindriktning_parser_handle_t *vindriktning_parser = (vindriktning_parser_handle_t *) args;
    
    vindriktning_read_event_t *event;
    for (;;)
    {
        if (xQueueReceive(vindriktning_parser->event_queue, &event, portMAX_DELAY))
        {
            ESP_LOGI(TAG, "AVG PM1=%0.1f, PM10=%0.1f, PM25=%0.1f", event->pm1, event->pm10, event->pm25);
        }
    }
}

void app_main(void)
{
    vindriktning_parser_config_t *parser_config = VINDRIKTNING_PARSER_CONFIG_DEFAULT();
    vindriktning_parser_handle_t *handle = vindriktning_parser_init(parser_config);

    ESP_LOGI(TAG, "Starting main task");
    
    BaseType_t err = xTaskCreate(main_task_entry, "main_task", 32768, handle, uxTaskPriorityGet(NULL), NULL);

    if (err != pdPASS) {
        ESP_LOGE(TAG, "Failed to create main task");
    } else {
        ESP_LOGI(TAG, "Created main task");
    }   
}