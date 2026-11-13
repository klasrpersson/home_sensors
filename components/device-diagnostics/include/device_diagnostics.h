#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "multicast_pubsub.h"

/**
 * Diagnostics messages are multicast pubsub messages of type "diagnostics", sent periodically to their own
 * group, with data like:
 *
 *   {"rssi":-66,"uptime":420}
 *
 * rssi:   signal strength of the Wi-Fi access point in dBm, left out when not connected
 * uptime: seconds since the device started
 */

/**
 * @brief Message type of diagnostics messages
 *
 */
#define DEVICE_DIAGNOSTICS_TYPE "diagnostics"

/**
 * @brief Longest interval between diagnostics messages, one day
 *
 */
#define DEVICE_DIAGNOSTICS_MAX_INTERVAL_S (86400)

/**
 * @brief Topic of diagnostics messages from the configured group and port, for receivers
 *
 */
#define DEVICE_DIAGNOSTICS_TOPIC_DEFAULT()                \
    {                                                     \
        .type = DEVICE_DIAGNOSTICS_TYPE,                  \
        .group = CONFIG_DEVICE_DIAGNOSTICS_GROUP,         \
        .port = CONFIG_DEVICE_DIAGNOSTICS_PORT            \
    }

/**
 * @brief Configuration of diagnostics messages
 *
 */
typedef struct {
    const char *group;   /*!< IPv4 multicast group diagnostics are sent to, e.g. "239.255.10.2" */
    uint16_t port;       /*!< UDP port */
    uint32_t interval_s; /*!< Seconds between messages, up to DEVICE_DIAGNOSTICS_MAX_INTERVAL_S, 0 sends none */
} device_diagnostics_config_t;

/**
 * @brief Default configuration of diagnostics messages, from menuconfig
 *
 */
#define DEVICE_DIAGNOSTICS_CONFIG_DEFAULT()                    \
    {                                                          \
        .group = CONFIG_DEVICE_DIAGNOSTICS_GROUP,              \
        .port = CONFIG_DEVICE_DIAGNOSTICS_PORT,                \
        .interval_s = CONFIG_DEVICE_DIAGNOSTICS_INTERVAL       \
    }

/**
 * @brief Send diagnostics messages
 *
 * Starts a task that publishes a diagnostics message every interval, the first one an interval after the
 * start. Can be called once.
 *
 * @param pubsub Handle of multicast pubsub to publish with, must have a source
 * @param config Configuration of diagnostics messages, the group is copied
 * @return ESP_OK if started, or if the interval is 0
 */
esp_err_t device_diagnostics_start(multicast_pubsub_handle_t *pubsub, const device_diagnostics_config_t *config);
