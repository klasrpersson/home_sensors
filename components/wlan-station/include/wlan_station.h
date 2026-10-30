#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/**
 * Defines WIFI_SSID and WIFI_PASSWORD, the Wi-Fi network set in wifi_credentials.h at the repository root
 */
#include WLAN_STATION_CREDENTIALS

/**
 * @brief Configuration of Wi-Fi station
 *
 */
typedef struct {
    const char *ssid;        /*!< Name of the network */
    const char *password;    /*!< Password of the network, empty for an open network */
    bool disable_power_save; /*!< Keep the radio on, so multicast messages are received without delay */
} wlan_station_config_t;

/**
 * @brief Start Wi-Fi and connect to a network
 *
 * Initializes NVS, the network stack and the default event loop. Returns without waiting for the
 * connection, and reconnects whenever the connection is lost. Failed attempts are retried after a delay
 * that doubles each time, up to a minute.
 *
 * @param config Configuration of Wi-Fi station
 * @return ESP_OK if Wi-Fi was started
 */
esp_err_t wlan_station_start(const wlan_station_config_t *config);

/**
 * @brief Wait until connected and given an IP address
 *
 * @param timeout Maximum time to wait
 * @return ESP_OK if connected, ESP_ERR_TIMEOUT if not connected in time
 */
esp_err_t wlan_station_wait_connected(TickType_t timeout);
