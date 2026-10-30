#pragma once

#include "esp_types.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"

#define IKEA_UART_PORT_NUM (CONFIG_UART_PORT_NUM)
#define IKEA_UART_BAUD_RATE (CONFIG_UART_BAUD_RATE)
#define IKEA_UART_RXD (CONFIG_UART_RXD)

/**
 * @brief Vindriktning parser library runtime structure
 *
 */
typedef struct {
    int measurement_no;
    uint16_t pm1[3];
    uint16_t pm10[3];
    uint16_t pm25[3];
    uart_port_t uart_port;                       /*!< Uart port number */
    char *buffer;                             /*!< Runtime buffer */
    TaskHandle_t tsk_hdl;                        /*!< P1 Parser task handle */
    QueueHandle_t event_queue;                   /*!< UART event queue handle */
} vindriktning_parser_handle_t;

typedef struct {
    uint16_t pm1;
    uint16_t pm10;
    uint16_t pm25;
} vindriktning_read_event_t;

/**
 * @brief Configuration of Vindriktning parser
 *
 */
typedef struct {
    struct {
        uart_port_t uart_port; /*!< UART port number */
        uint32_t baud_rate;           /*!< UART baud rate */
        uart_word_length_t data_bits; /*!< UART data bits length */
        uart_parity_t parity;         /*!< UART parity */
        uart_stop_bits_t stop_bits;   /*!< UART stop bits length */
        uint32_t event_queue_size;    /*!< UART event queue size */
    } uart;                               /*!< UART specific configuration */
} vindriktning_parser_config_t;

/**
 * @brief Default configuration for Vindriktning parser
 *
 */
#define VINDRIKTNING_PARSER_CONFIG_DEFAULT()         \
    {                                      \
        .uart = {                          \
            .uart_port = IKEA_UART_PORT_NUM, \
            .baud_rate = IKEA_UART_BAUD_RATE,           \
            .data_bits = UART_DATA_8_BITS, \
            .parity = UART_PARITY_DISABLE, \
            .stop_bits = UART_STOP_BITS_1, \
            .event_queue_size = 16         \
        }                                  \
    }

/**
 * @brief Init Vindriktning Parser
 *
 * @param config Configuration of Vindriktning Parser
 * @return p1_parser_handle_t handle of Vindriktning parser
 */
vindriktning_parser_handle_t *vindriktning_parser_init(const vindriktning_parser_config_t *config);