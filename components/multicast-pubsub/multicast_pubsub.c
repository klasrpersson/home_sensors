#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "lwip/sockets.h"
#include "esp_log.h"
#include "multicast_pubsub.h"

static const char *TAG = "multicast_pubsub";

struct multicast_pubsub {
    int sock;     /*!< UDP socket used for sending */
    char *source; /*!< Name of this device */
};

/**
 * @brief Check that a string can be put in a JSON string without escaping
 *
 * @param str string to check
 */
static bool is_json_safe(const char *str) {
    if (str == NULL || *str == '\0') {
        return false;
    }
    for (; *str != '\0'; str++) {
        if (*str == '"' || *str == '\\' || (unsigned char) *str < 0x20) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Init multicast pubsub
 *
 * @param config Configuration of multicast pubsub
 * @return multicast_pubsub_handle_t handle of multicast pubsub, NULL on failure
 */
multicast_pubsub_handle_t *multicast_pubsub_init(const multicast_pubsub_config_t *config) {
    if (!is_json_safe(config->source)) {
        ESP_LOGE(TAG, "source must be a non-empty string without quotes, backslashes or control characters");
        return NULL;
    }

    multicast_pubsub_handle_t *pubsub = calloc(1, sizeof(multicast_pubsub_handle_t));
    if (!pubsub) {
        ESP_LOGE(TAG, "calloc memory for pubsub failed");
        goto err_pubsub;
    }
    pubsub->source = strdup(config->source);
    if (!pubsub->source) {
        ESP_LOGE(TAG, "copy of source failed");
        goto err_source;
    }

    pubsub->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (pubsub->sock < 0) {
        ESP_LOGE(TAG, "create socket failed, errno %d", errno);
        goto err_source;
    }

    uint8_t ttl = config->ttl;
    if (setsockopt(pubsub->sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        ESP_LOGE(TAG, "set multicast TTL failed, errno %d", errno);
        goto err_sock;
    }

    ESP_LOGI(TAG, "Multicast pubsub init OK");

    return pubsub;

    /*Error Handling*/
    err_sock:
    close(pubsub->sock);
    err_source:
    free(pubsub->source);
    free(pubsub);
    err_pubsub:
    return NULL;
}

/**
 * @brief Publish a message to the multicast group of a topic
 *
 * @param handle Handle of multicast pubsub
 * @param topic Topic to publish to, its sequence number is incremented
 * @param data JSON object with the message data
 * @return ESP_OK if the message was sent
 */
esp_err_t multicast_pubsub_publish(multicast_pubsub_handle_t *handle, multicast_pubsub_topic_t *topic, const char *data) {
    if (handle == NULL || topic == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!is_json_safe(topic->type)) {
        ESP_LOGE(TAG, "type must be a non-empty string without quotes, backslashes or control characters");
        return ESP_ERR_INVALID_ARG;
    }

    struct sockaddr_in dest = {
            .sin_family = AF_INET,
            .sin_port = htons(topic->port)
    };
    if (topic->group == NULL || inet_aton(topic->group, &dest.sin_addr) == 0 || !IN_MULTICAST(ntohl(dest.sin_addr.s_addr))) {
        ESP_LOGE(TAG, "invalid multicast group for type %s", topic->type);
        return ESP_ERR_INVALID_ARG;
    }

    char message[MULTICAST_PUBSUB_MAX_MESSAGE_SIZE];
    int len = snprintf(message, sizeof(message), "{\"v\":%d,\"type\":\"%s\",\"src\":\"%s\",\"seq\":%" PRIu32 ",\"data\":%s}",
                       MULTICAST_PUBSUB_MESSAGE_VERSION, topic->type, handle->source, topic->seq, data);
    if (len < 0 || len >= (int) sizeof(message)) {
        ESP_LOGE(TAG, "message of type %s does not fit in %d bytes", topic->type, MULTICAST_PUBSUB_MAX_MESSAGE_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    // Count lost sends too, so receivers see them as gaps
    topic->seq++;

    if (sendto(handle->sock, message, len, 0, (struct sockaddr *) &dest, sizeof(dest)) < 0) {
        ESP_LOGW(TAG, "send of type %s to %s:%u failed, errno %d", topic->type, topic->group, topic->port, errno);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Sent %s", message);

    return ESP_OK;
}
