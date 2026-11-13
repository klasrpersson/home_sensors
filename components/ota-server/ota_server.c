#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_https_server.h"
#include "ota_server.h"

static const char *TAG = "ota_server";

/**
 * @brief Size of the buffer an upload is received in
 *
 */
#define OTA_SERVER_BUFFER_SIZE (4096)

/**
 * @brief Seconds to wait for more of an upload, and how many times, before giving up
 *
 */
#define OTA_SERVER_RECV_TIMEOUT_S (10)
#define OTA_SERVER_RECV_TIMEOUT_RETRIES (3)

/**
 * @brief Bytes at the start of an app image that hold its description
 *
 */
#define OTA_SERVER_IMAGE_HEADER_SIZE (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))

#define OTA_SERVER_RESTART_DELAY_MS (1000)

extern const uint8_t server_cert_start[] asm("_binary_ota_server_cert_pem_start");
extern const uint8_t server_cert_end[] asm("_binary_ota_server_cert_pem_end");
extern const uint8_t server_key_start[] asm("_binary_ota_server_key_pem_start");
extern const uint8_t server_key_end[] asm("_binary_ota_server_key_pem_end");
extern const char token_start[] asm("_binary_ota_token_start");

static httpd_handle_t server;
static size_t token_len;
static esp_timer_handle_t confirm_timer;

/**
 * @brief Check the token of a request, in constant time
 *
 * @param req Request
 * @return true if the request has the right token
 */
static bool is_authorized(httpd_req_t *req) {
    static const char prefix[] = "Bearer ";
    char auth[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth, sizeof(auth)) != ESP_OK) {
        return false;
    }
    if (strncmp(auth, prefix, sizeof(prefix) - 1) != 0) {
        return false;
    }

    const char *given = auth + sizeof(prefix) - 1;
    if (strlen(given) != token_len) {
        return false;
    }
    uint8_t diff = 0;
    for (size_t i = 0; i < token_len; i++) {
        diff |= (uint8_t) given[i] ^ (uint8_t) token_start[i];
    }
    return diff == 0;
}

/**
 * @brief Send an error response, after which the connection is closed
 *
 * @param req Request
 * @param status HTTP status, e.g. "400 Bad Request"
 * @param message Explanation for the sender
 * @return ESP_FAIL, so the server closes the connection without reading the rest of the upload
 */
static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message) {
    ESP_LOGW(TAG, "Update refused: %s", message);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, message);
    return ESP_FAIL;
}

/**
 * @brief Check that an image is an app of the same project as the running one
 *
 * Signed images of other firmwares in the repository are valid too, but would leave the device without
 * the function it has.
 *
 * @param image Start of the image, at least OTA_SERVER_IMAGE_HEADER_SIZE bytes
 * @param error Set to the reason if not
 * @return true if the image belongs to the running project
 */
static bool is_same_project(const char *image, const char **error) {
    const esp_app_desc_t *new_desc = (const esp_app_desc_t *) (image + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    if (new_desc->magic_word != ESP_APP_DESC_MAGIC_WORD) {
        *error = "Not an app image";
        return false;
    }

    const esp_app_desc_t *running_desc = esp_app_get_description();
    if (strncmp(new_desc->project_name, running_desc->project_name, sizeof(new_desc->project_name)) != 0) {
        *error = "Image is for another firmware";
        return false;
    }

    ESP_LOGI(TAG, "Receiving %s version %.32s, running version %s", running_desc->project_name, new_desc->version,
             running_desc->version);
    return true;
}

/**
 * @brief Handle POST /ota: receive an app image and boot it
 *
 * @param req Request
 * @return ESP_OK if the response was sent, ESP_FAIL to close the connection
 */
static esp_err_t ota_post_handler(httpd_req_t *req) {
    if (!is_authorized(req)) {
        return send_error(req, "401 Unauthorized", "Missing or wrong token");
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL) {
        return send_error(req, "500 Internal Server Error", "No OTA partition to write to");
    }
    if (req->content_len < OTA_SERVER_IMAGE_HEADER_SIZE || req->content_len > partition->size) {
        return send_error(req, "400 Bad Request", "Image size does not fit the OTA partition");
    }

    char *buffer = malloc(OTA_SERVER_BUFFER_SIZE);
    if (buffer == NULL) {
        return send_error(req, "500 Internal Server Error", "Out of memory");
    }

    esp_err_t result = ESP_FAIL;
    esp_ota_handle_t ota = 0;
    bool started = false;
    size_t received = 0;
    size_t filled = 0;
    int timeouts = 0;
    const char *error = NULL;

    ESP_LOGI(TAG, "Receiving an update of %u bytes into %s", (unsigned) req->content_len, partition->label);

    while (received < req->content_len) {
        int len = httpd_req_recv(req, buffer + filled, OTA_SERVER_BUFFER_SIZE - filled);
        if (len == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= OTA_SERVER_RECV_TIMEOUT_RETRIES) {
            continue;
        }
        if (len <= 0) {
            ESP_LOGW(TAG, "Update aborted, receive failed after %u bytes", (unsigned) received);
            goto done;
        }
        timeouts = 0;
        received += len;
        filled += len;

        if (!started) {
            // Check the image description before erasing anything
            if (filled < OTA_SERVER_IMAGE_HEADER_SIZE) {
                continue;
            }
            if (!is_same_project(buffer, &error)) {
                result = send_error(req, "400 Bad Request", error);
                goto done;
            }

            esp_err_t err = esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &ota);
            if (err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
                result = send_error(req, "409 Conflict", "Running firmware is not confirmed yet");
                goto done;
            }
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
                result = send_error(req, "500 Internal Server Error", "Could not start the update");
                goto done;
            }
            started = true;
        }

        esp_err_t err = esp_ota_write(ota, buffer, filled);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            result = send_error(req, "500 Internal Server Error", "Could not write the update");
            goto done;
        }
        filled = 0;
    }

    // Checks the image, and its signature with the key the running firmware was signed with
    esp_err_t err = esp_ota_end(ota);
    started = false;
    if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
        result = send_error(req, "400 Bad Request", "Image is invalid or not signed with the right key");
        goto done;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        result = send_error(req, "500 Internal Server Error", "Could not finish the update");
        goto done;
    }

    err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        result = send_error(req, "500 Internal Server Error", "Could not select the new firmware");
        goto done;
    }

    ESP_LOGI(TAG, "Update written, restarting");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Updated, restarting");
    free(buffer);

    vTaskDelay(pdMS_TO_TICKS(OTA_SERVER_RESTART_DELAY_MS));
    esp_restart();

    done:
    if (started) {
        esp_ota_abort(ota);
    }
    free(buffer);
    return result;
}

/**
 * @brief Called when a new firmware did not confirm in time
 *
 * @param arg unused
 */
static void confirm_timeout(void *arg) {
    ESP_LOGE(TAG, "New firmware not confirmed within %d s, going back to the previous firmware",
             CONFIG_OTA_SERVER_CONFIRM_TIMEOUT);
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

/**
 * @brief Check if the running firmware still has to be confirmed
 *
 */
static bool is_pending_verify(void) {
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

/**
 * @brief Start the update server
 *
 * @return ESP_OK if started
 */
esp_err_t ota_server_start(void) {
    if (server != NULL) {
        ESP_LOGE(TAG, "already started");
        return ESP_ERR_INVALID_STATE;
    }

    // The token file may end with a newline
    token_len = strlen(token_start);
    while (token_len > 0 && isspace((unsigned char) token_start[token_len - 1])) {
        token_len--;
    }
    if (token_len == 0) {
        ESP_LOGE(TAG, "token is empty, run tools/generate_ota_keys.sh");
        return ESP_ERR_INVALID_STATE;
    }

    if (is_pending_verify() && confirm_timer == NULL) {
        esp_timer_create_args_t timer_args = {
                .callback = confirm_timeout,
                .name = "ota_confirm"
        };
        esp_err_t err = esp_timer_create(&timer_args, &confirm_timer);
        if (err == ESP_OK) {
            err = esp_timer_start_once(confirm_timer, (uint64_t) CONFIG_OTA_SERVER_CONFIRM_TIMEOUT * 1000000);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "start of confirm timer failed: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGW(TAG, "Running a new firmware, it must confirm within %d s", CONFIG_OTA_SERVER_CONFIRM_TIMEOUT);
    }

    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.servercert = server_cert_start;
    config.servercert_len = server_cert_end - server_cert_start;
    config.prvtkey_pem = server_key_start;
    config.prvtkey_len = server_key_end - server_key_start;
    config.port_secure = CONFIG_OTA_SERVER_PORT;
    // One TLS connection at a time, each takes tens of kB of RAM. A new connection closes an idle one.
    config.httpd.max_open_sockets = 1;
    config.httpd.lru_purge_enable = true;
    config.httpd.recv_wait_timeout = OTA_SERVER_RECV_TIMEOUT_S;

    esp_err_t err = httpd_ssl_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start of HTTPS server failed: %s", esp_err_to_name(err));
        server = NULL;
        return err;
    }

    httpd_uri_t ota_uri = {
            .uri = "/ota",
            .method = HTTP_POST,
            .handler = ota_post_handler
    };
    err = httpd_register_uri_handler(server, &ota_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register of /ota failed: %s", esp_err_to_name(err));
        httpd_ssl_stop(server);
        server = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Waiting for updates on port %d, running version %s", CONFIG_OTA_SERVER_PORT,
             esp_app_get_description()->version);

    return ESP_OK;
}

/**
 * @brief Confirm that the running firmware works
 *
 * @return ESP_OK if the firmware is confirmed
 */
esp_err_t ota_server_mark_valid(void) {
    if (!is_pending_verify()) {
        return ESP_OK;
    }

    if (confirm_timer != NULL) {
        esp_timer_stop(confirm_timer);
    }
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "confirm of the new firmware failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "New firmware confirmed");
    return ESP_OK;
}
