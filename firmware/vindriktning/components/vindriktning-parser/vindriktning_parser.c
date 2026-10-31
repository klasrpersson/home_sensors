#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "vindriktning_parser.h"

#include "freertos/queue.h"

static const char *TAG = "vindriktning_parser";

/**
 * @brief Vindriktning Parser runtime buffer size
 *
 */
#define VINDRIKTNING_PARSER_RUNTIME_BUFFER_SIZE (512)

#define VINDRIKTNING_PARSER_RING_BUFFER_SIZE (1024)

#define VINDRIKTNING_DATASET_SIZE (20)

static bool is_valid(char *data, int len) {
    if (len != VINDRIKTNING_DATASET_SIZE)
    {
        ESP_LOGE(TAG, "Invalid data length. (%d)", len);
        return false;
    }
    
    // if (data[0] == 0x16 && data[1] == 0x11 && data[2] == 0x0b)
    // {
    //     ESP_LOGE(TAG, "Invalid header. (%x %x %x)", data[0], data[1], data[2]);
    //     return false;
    // }
        
    uint8_t crc = 0;
    for (uint32_t i = 0; i < len; i++) {
        crc += data[i];
    }
    if (crc != 0) {
        ESP_LOGE(TAG, "Invalid checksum. (%d)", crc);
        return false;
    }
    
    return true;
}

/**
 * @brief Vindriktning Parser Task Entry
 *
 * @param arg argument
 */
static void vindriktning_parser_task_entry(void *arg) {
    vindriktning_parser_handle_t *esp_vindriktning = (vindriktning_parser_handle_t *) arg;

    while (1) {
        int len = uart_read_bytes(esp_vindriktning->uart_port, esp_vindriktning->buffer, (VINDRIKTNING_PARSER_RUNTIME_BUFFER_SIZE - 1), 20 / portTICK_PERIOD_MS);

        if (len <= 0)
        {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }
            
        if (!is_valid(esp_vindriktning->buffer, len))
        {
            ESP_LOG_BUFFER_HEXDUMP(TAG, esp_vindriktning->buffer, len, ESP_LOG_ERROR);
            continue;
        }

        // TODO: If last measurement was longer than 5 s ago then set measurements->measurement_no to 0

        // sample data:
        //  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19
        // 16 11 0b 00 00 00 0c 00 00 03 cb 00 00 00 0c 01 00 00 00 e7
        //               |pm2_5|     |pm1_0|     |pm10 |        | CRC |

        uint16_t pm25 = esp_vindriktning->buffer[5] << 8 | esp_vindriktning->buffer[6];
        uint16_t pm1 = esp_vindriktning->buffer[9] << 8 | esp_vindriktning->buffer[10];
        uint16_t pm10 = esp_vindriktning->buffer[13] << 8 | esp_vindriktning->buffer[14];

        ESP_LOGD(TAG, "PM1=%d", pm1);
        ESP_LOGD(TAG, "PM10=%d", pm10);
        ESP_LOGD(TAG, "PM25=%d", pm25);

        esp_vindriktning->pm1[esp_vindriktning->measurement_no] = pm1;
        esp_vindriktning->pm10[esp_vindriktning->measurement_no] = pm10;
        esp_vindriktning->pm25[esp_vindriktning->measurement_no] = pm25;
        if (esp_vindriktning->measurement_count < VINDRIKTNING_PARSER_AVERAGE_SAMPLES) {
            esp_vindriktning->measurement_count++;
        }

        // Average only the samples received so far, so the first readings are not pulled towards zero
        int count = esp_vindriktning->measurement_count;
        uint32_t sum_pm1 = 0;
        uint32_t sum_pm10 = 0;
        uint32_t sum_pm25 = 0;
        for (int i = 0; i < count; ++i) {
            sum_pm1 += esp_vindriktning->pm1[i];
            sum_pm10 += esp_vindriktning->pm10[i];
            sum_pm25 += esp_vindriktning->pm25[i];
        }

        vindriktning_read_event_t evt = {
            .pm1 = (sum_pm1 + count / 2) / count,
            .pm10 = (sum_pm10 + count / 2) / count,
            .pm25 = (sum_pm25 + count / 2) / count
        };

        xQueueSend(esp_vindriktning->event_queue, &evt, pdMS_TO_TICKS(100));

        esp_vindriktning->measurement_no = (esp_vindriktning->measurement_no + 1) % VINDRIKTNING_PARSER_AVERAGE_SAMPLES;
    }
}

/**
 * @brief Init Vindriktning Parser
 *
 * @param config Configuration of Vindriktning Parser
 * @return vindriktning_parser_handle_t handle of vindriktning parser
 */
vindriktning_parser_handle_t *vindriktning_parser_init(const vindriktning_parser_config_t *config) {
    vindriktning_parser_handle_t *esp_vindriktning = calloc(1, sizeof(vindriktning_parser_handle_t));
    if (!esp_vindriktning) {
        ESP_LOGE(TAG, "calloc memory for esp_vindriktning failed");
        goto err_handle;
    }
    esp_vindriktning->buffer = calloc(1, VINDRIKTNING_PARSER_RUNTIME_BUFFER_SIZE);
    if (!esp_vindriktning->buffer) {
        ESP_LOGE(TAG, "calloc memory for runtime buffer failed");
        goto err_buffer;
    }

    /* Set attributes */
    esp_vindriktning->uart_port = config->uart.uart_port;

    esp_vindriktning->event_queue = xQueueCreate(config->uart.event_queue_size, sizeof(vindriktning_read_event_t));
    if (!esp_vindriktning->event_queue) {
        ESP_LOGE(TAG, "create event queue failed");
        goto err_queue;
    }

    /* Install UART driver */
    uart_config_t uart_config = {
            .baud_rate = config->uart.baud_rate,
            .data_bits = config->uart.data_bits,
            .parity = config->uart.parity,
            .stop_bits = config->uart.stop_bits,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .source_clk = UART_SCLK_DEFAULT
    };

    if (uart_driver_install(esp_vindriktning->uart_port, VINDRIKTNING_PARSER_RING_BUFFER_SIZE, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "install uart driver failed");
        goto err_uart_install;
    }

    if (uart_param_config(esp_vindriktning->uart_port, &uart_config) != ESP_OK) {
        ESP_LOGE(TAG, "config uart parameter failed");
        goto err_uart_config;
    }

    if (uart_set_pin(esp_vindriktning->uart_port, UART_PIN_NO_CHANGE, IKEA_UART_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "config uart gpio failed");
        goto err_uart_config;
    }

    /* Create Vindriktning Parser task */
    BaseType_t err = xTaskCreate(
            vindriktning_parser_task_entry,
            "vindriktning_parser",
            3072,
            esp_vindriktning,
            12,
            &esp_vindriktning->tsk_hdl);
    if (err != pdTRUE) {
        ESP_LOGE(TAG, "create Vindriktning Parser task failed");
        goto err_task_create;
    }
    ESP_LOGI(TAG, "Vindriktning Parser init OK");

    return esp_vindriktning;

    /*Error Handling*/
    err_task_create:
    err_uart_config:
    uart_driver_delete(esp_vindriktning->uart_port);
    err_uart_install:
    vQueueDelete(esp_vindriktning->event_queue);
    err_queue:
    free(esp_vindriktning->buffer);
    err_buffer:
    free(esp_vindriktning);
    err_handle:
    return NULL;
}