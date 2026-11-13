#pragma once

#include "esp_err.h"

/**
 * Over-the-air updates, pushed to the device with tools/ota_push.py:
 *
 *   POST /ota with "Authorization: Bearer <token>" and the app image as the body
 *
 * The server only accepts HTTPS, with the certificate from keys/ota_server_cert.pem. The image is written to
 * the OTA partition that is not running, and the device only boots it if its signature is valid. A new
 * firmware has to call ota_server_mark_valid() within CONFIG_OTA_SERVER_CONFIRM_TIMEOUT seconds, or the device
 * goes back to the previous firmware. It also goes back if it restarts before confirming.
 *
 * Requires the app rollback and signed app options, see the firmware's sdkconfig.defaults.
 */

/**
 * @brief Start the update server
 *
 * Call once the network is up. If the running firmware is new and not confirmed yet, also starts the
 * confirm timeout.
 *
 * @return ESP_OK if started
 */
esp_err_t ota_server_start(void);

/**
 * @brief Confirm that the running firmware works
 *
 * Call when the firmware has shown that it works, e.g. after its first message was sent. Does nothing if the
 * firmware is already confirmed. Updates are refused until the running firmware is confirmed.
 *
 * @return ESP_OK if the firmware is confirmed
 */
esp_err_t ota_server_mark_valid(void);
