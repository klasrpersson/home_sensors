#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "esp_log.h"
#include "multicast_pubsub.h"

static const char *TAG = "multicast_pubsub";

/**
 * @brief Receive task stack size, callbacks run on this stack next to the receive buffer
 *
 */
#define MULTICAST_PUBSUB_RX_TASK_STACK_SIZE (4096 + MULTICAST_PUBSUB_MAX_MESSAGE_SIZE + 1)

#define MULTICAST_PUBSUB_RX_TASK_PRIORITY (5)

/**
 * @brief How often the receive task picks up new subscriptions
 *
 */
#define MULTICAST_PUBSUB_RX_POLL_MS (1000)

typedef struct {
    int sock;                             /*!< UDP socket bound to the topic group */
    char *type;                           /*!< Message type of the topic */
    multicast_pubsub_callback_t callback; /*!< Called for each received message */
    void *ctx;                            /*!< Passed to the callback */
} multicast_pubsub_subscription_t;

struct multicast_pubsub {
    int sock;                    /*!< UDP socket used for sending, -1 if only subscribing */
    char *source;                /*!< Name of this device, NULL if only subscribing */
    bool send_failing;           /*!< The last send failed, further failures are not logged */
    SemaphoreHandle_t lock;      /*!< Protects subscription_count and rx_task */
    TaskHandle_t rx_task;        /*!< Receive task handle, created by the first subscription */
    size_t subscription_count;   /*!< Subscriptions are only added, entries below the count do not change */
    multicast_pubsub_subscription_t subscriptions[MULTICAST_PUBSUB_MAX_SUBSCRIPTIONS];
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
 * @brief Check that JSON is nested no deeper than MULTICAST_PUBSUB_MAX_DEPTH
 *
 * cJSON parses, prints and deletes recursively, so a deeply nested message would overflow the receive
 * task stack. Only the part before the first unbalanced bracket is checked, as cJSON stops parsing there.
 *
 * @param json JSON to check
 * @param len Length of the JSON
 */
static bool is_shallow_json(const char *json, size_t len) {
    int depth = 0;
    bool in_string = false;
    for (size_t i = 0; i < len; i++) {
        if (in_string) {
            if (json[i] == '\\') {
                i++;
            } else if (json[i] == '"') {
                in_string = false;
            }
        } else if (json[i] == '"') {
            in_string = true;
        } else if (json[i] == '{' || json[i] == '[') {
            if (++depth > MULTICAST_PUBSUB_MAX_DEPTH) {
                return false;
            }
        } else if (json[i] == '}' || json[i] == ']') {
            if (--depth < 0) {
                break;
            }
        }
    }
    return true;
}

/**
 * @brief Get the multicast group address and port of a topic
 *
 * @param topic Topic
 * @param addr Filled with the address
 * @return ESP_OK if the topic has a valid type and multicast group
 */
static esp_err_t topic_address(const multicast_pubsub_topic_t *topic, struct sockaddr_in *addr) {
    if (!is_json_safe(topic->type)) {
        ESP_LOGE(TAG, "type must be a non-empty string without quotes, backslashes or control characters");
        return ESP_ERR_INVALID_ARG;
    }

    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(topic->port);
    if (topic->group == NULL || inet_aton(topic->group, &addr->sin_addr) == 0 || !IN_MULTICAST(ntohl(addr->sin_addr.s_addr))) {
        ESP_LOGE(TAG, "invalid multicast group for type %s", topic->type);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

/**
 * @brief Check that a topic has a valid type and multicast group
 *
 * @param topic Topic to check
 * @return ESP_OK if the topic is valid
 */
esp_err_t multicast_pubsub_check_topic(const multicast_pubsub_topic_t *topic) {
    if (topic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    struct sockaddr_in addr;
    return topic_address(topic, &addr);
}

/**
 * @brief Init multicast pubsub
 *
 * @param config Configuration of multicast pubsub
 * @return multicast_pubsub_handle_t handle of multicast pubsub, NULL on failure
 */
multicast_pubsub_handle_t *multicast_pubsub_init(const multicast_pubsub_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "config must be set");
        return NULL;
    }
    if (config->source != NULL && !is_json_safe(config->source)) {
        ESP_LOGE(TAG, "source must be a non-empty string without quotes, backslashes or control characters");
        return NULL;
    }

    multicast_pubsub_handle_t *pubsub = calloc(1, sizeof(multicast_pubsub_handle_t));
    if (!pubsub) {
        ESP_LOGE(TAG, "calloc memory for pubsub failed");
        goto err_pubsub;
    }
    if (config->source != NULL) {
        pubsub->source = strdup(config->source);
        if (!pubsub->source) {
            ESP_LOGE(TAG, "copy of source failed");
            goto err_source;
        }
    }

    pubsub->lock = xSemaphoreCreateMutex();
    if (!pubsub->lock) {
        ESP_LOGE(TAG, "create lock failed");
        goto err_source;
    }

    // Only publishing sends from this socket, subscriptions open one each
    pubsub->sock = -1;
    if (pubsub->source != NULL) {
        pubsub->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (pubsub->sock < 0) {
            ESP_LOGE(TAG, "create socket failed, errno %d", errno);
            goto err_lock;
        }

        uint8_t ttl = config->ttl;
        if (setsockopt(pubsub->sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
            ESP_LOGE(TAG, "set multicast TTL failed, errno %d", errno);
            goto err_sock;
        }
    }

    ESP_LOGI(TAG, "Multicast pubsub init OK");

    return pubsub;

    /*Error Handling*/
    err_sock:
    close(pubsub->sock);
    err_lock:
    vSemaphoreDelete(pubsub->lock);
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
    if (handle->source == NULL) {
        ESP_LOGE(TAG, "source must be set in the config to publish");
        return ESP_ERR_INVALID_STATE;
    }

    struct sockaddr_in dest;
    esp_err_t err = topic_address(topic, &dest);
    if (err != ESP_OK) {
        return err;
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
        // Sends fail for as long as Wi-Fi is down, so only log the first failure
        if (!handle->send_failing) {
            ESP_LOGW(TAG, "send of type %s to %s:%u failed, errno %d, further failures are not logged until a send succeeds",
                     topic->type, topic->group, topic->port, errno);
            handle->send_failing = true;
        }
        return ESP_FAIL;
    }
    if (handle->send_failing) {
        ESP_LOGI(TAG, "Sending again");
        handle->send_failing = false;
    }

    ESP_LOGD(TAG, "Sent %s", message);

    return ESP_OK;
}

/**
 * @brief Receive one message of a subscription and pass it to the callback
 *
 * @param subscription Subscription with a readable socket
 * @param buffer Receive buffer of MULTICAST_PUBSUB_MAX_MESSAGE_SIZE + 1 bytes
 */
static void receive_message(const multicast_pubsub_subscription_t *subscription, char *buffer) {
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    int len = recvfrom(subscription->sock, buffer, MULTICAST_PUBSUB_MAX_MESSAGE_SIZE, 0, (struct sockaddr *) &from, &from_len);
    if (len < 0) {
        ESP_LOGW(TAG, "receive of type %s failed, errno %d", subscription->type, errno);
        return;
    }
    buffer[len] = '\0';

    char sender[INET_ADDRSTRLEN] = "?";
    inet_ntop(AF_INET, &from.sin_addr, sender, sizeof(sender));

    if (!is_shallow_json(buffer, len)) {
        ESP_LOGW(TAG, "dropped message nested deeper than %d from %s", MULTICAST_PUBSUB_MAX_DEPTH, sender);
        return;
    }

    cJSON *root = cJSON_ParseWithLength(buffer, len);
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "v");
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    const cJSON *source = cJSON_GetObjectItemCaseSensitive(root, "src");
    const cJSON *seq = cJSON_GetObjectItemCaseSensitive(root, "seq");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");

    // Hold received messages to the same rules as sent ones: a plain source name and a whole sequence number
    if (!cJSON_IsNumber(version) || !cJSON_IsString(type) || !cJSON_IsString(source) || !is_json_safe(source->valuestring) ||
        !cJSON_IsNumber(seq) || !cJSON_IsObject(data) ||
        seq->valuedouble < 0 || seq->valuedouble > UINT32_MAX || seq->valuedouble != (uint32_t) seq->valuedouble) {
        ESP_LOGW(TAG, "dropped invalid message from %s", sender);
    } else if (version->valuedouble != MULTICAST_PUBSUB_MESSAGE_VERSION) {
        ESP_LOGW(TAG, "dropped message with unsupported version %g from %s", version->valuedouble, sender);
    } else if (strcmp(type->valuestring, subscription->type) != 0) {
        // Expected when several types share a group
        ESP_LOGD(TAG, "dropped message of type %s from %s, expected %s", type->valuestring, sender, subscription->type);
    } else {
        multicast_pubsub_message_t message = {
                .type = type->valuestring,
                .source = source->valuestring,
                .seq = (uint32_t) seq->valuedouble,
                .data = data,
                .sender = sender
        };
        subscription->callback(&message, subscription->ctx);
    }

    cJSON_Delete(root);
}

/**
 * @brief Multicast Pubsub Receive Task Entry
 *
 * @param arg argument
 */
static void multicast_pubsub_rx_task_entry(void *arg) {
    multicast_pubsub_handle_t *pubsub = (multicast_pubsub_handle_t *) arg;
    char buffer[MULTICAST_PUBSUB_MAX_MESSAGE_SIZE + 1];

    while (1) {
        xSemaphoreTake(pubsub->lock, portMAX_DELAY);
        size_t count = pubsub->subscription_count;
        xSemaphoreGive(pubsub->lock);

        fd_set readable;
        FD_ZERO(&readable);
        int max_sock = -1;
        for (size_t i = 0; i < count; i++) {
            FD_SET(pubsub->subscriptions[i].sock, &readable);
            if (pubsub->subscriptions[i].sock > max_sock) {
                max_sock = pubsub->subscriptions[i].sock;
            }
        }

        // Time out now and then to pick up new subscriptions
        struct timeval timeout = {
                .tv_sec = MULTICAST_PUBSUB_RX_POLL_MS / 1000,
                .tv_usec = (MULTICAST_PUBSUB_RX_POLL_MS % 1000) * 1000
        };
        int ready = select(max_sock + 1, &readable, NULL, NULL, &timeout);
        if (ready < 0) {
            ESP_LOGE(TAG, "select failed, errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(MULTICAST_PUBSUB_RX_POLL_MS));
            continue;
        }

        for (size_t i = 0; i < count && ready > 0; i++) {
            if (FD_ISSET(pubsub->subscriptions[i].sock, &readable)) {
                receive_message(&pubsub->subscriptions[i], buffer);
                ready--;
            }
        }
    }
}

/**
 * @brief Join the multicast group of a topic and receive its messages
 *
 * @param handle Handle of multicast pubsub
 * @param topic Topic to subscribe to
 * @param callback Called for each received message
 * @param ctx Passed to the callback
 * @return ESP_OK if subscribed
 */
esp_err_t multicast_pubsub_subscribe(multicast_pubsub_handle_t *handle, const multicast_pubsub_topic_t *topic,
                                     multicast_pubsub_callback_t callback, void *ctx) {
    if (handle == NULL || topic == NULL || callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    struct sockaddr_in group;
    esp_err_t err = topic_address(topic, &group);
    if (err != ESP_OK) {
        return err;
    }

    multicast_pubsub_subscription_t subscription = {
            .sock = -1,
            .callback = callback,
            .ctx = ctx
    };

    xSemaphoreTake(handle->lock, portMAX_DELAY);

    if (handle->subscription_count >= MULTICAST_PUBSUB_MAX_SUBSCRIPTIONS) {
        ESP_LOGE(TAG, "subscription limit of %d reached", MULTICAST_PUBSUB_MAX_SUBSCRIPTIONS);
        err = ESP_ERR_NO_MEM;
        goto err_limit;
    }

    subscription.type = strdup(topic->type);
    if (!subscription.type) {
        ESP_LOGE(TAG, "copy of type failed");
        err = ESP_ERR_NO_MEM;
        goto err_limit;
    }

    subscription.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (subscription.sock < 0) {
        ESP_LOGE(TAG, "create socket failed, errno %d", errno);
        err = ESP_FAIL;
        goto err_type;
    }

    // Allow several subscriptions on the same port
    int reuse = 1;
    if (setsockopt(subscription.sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        ESP_LOGE(TAG, "set address reuse failed, errno %d", errno);
        err = ESP_FAIL;
        goto err_sock;
    }

    // Bind to the group address, so the socket only gets messages sent to this group
    if (bind(subscription.sock, (struct sockaddr *) &group, sizeof(group)) < 0) {
        ESP_LOGE(TAG, "bind to %s:%u failed, errno %d", topic->group, topic->port, errno);
        err = ESP_FAIL;
        goto err_sock;
    }

    struct ip_mreq membership = {
            .imr_multiaddr = group.sin_addr,
            .imr_interface.s_addr = htonl(INADDR_ANY)
    };
    if (setsockopt(subscription.sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) < 0) {
        ESP_LOGE(TAG, "join of group %s failed, errno %d", topic->group, errno);
        err = ESP_FAIL;
        goto err_sock;
    }

    handle->subscriptions[handle->subscription_count] = subscription;
    handle->subscription_count++;

    // Created while holding the lock, so the task starts with this subscription
    if (handle->rx_task == NULL) {
        BaseType_t created = xTaskCreate(
                multicast_pubsub_rx_task_entry,
                "multicast_rx",
                MULTICAST_PUBSUB_RX_TASK_STACK_SIZE,
                handle,
                MULTICAST_PUBSUB_RX_TASK_PRIORITY,
                &handle->rx_task);
        if (created != pdPASS) {
            ESP_LOGE(TAG, "create receive task failed");
            handle->subscription_count--;
            err = ESP_ERR_NO_MEM;
            goto err_sock;
        }
    }

    xSemaphoreGive(handle->lock);

    ESP_LOGI(TAG, "Subscribed to %s on %s:%u", topic->type, topic->group, topic->port);

    return ESP_OK;

    /*Error Handling*/
    err_sock:
    close(subscription.sock);
    err_type:
    free(subscription.type);
    err_limit:
    xSemaphoreGive(handle->lock);
    return err;
}
