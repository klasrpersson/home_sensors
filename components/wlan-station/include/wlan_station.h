#pragma once

#include "esp_err.h"

/**
 * @brief Configuration of Wi-Fi station
 *
 */
typedef struct {
    const char *ssid;     /*!< Name of the network */
    const char *password; /*!< Password of the network, empty for an open network */
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
