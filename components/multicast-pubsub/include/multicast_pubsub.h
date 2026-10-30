#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "cJSON.h"

/**
 * Messages are sent as UDP datagrams to the multicast group of their type, as JSON:
 *
 *   {"v":1,"type":"air_quality","src":"livingroom","seq":42,"data":{"pm1":3,"pm10":6,"pm25":5}}
 *
 * v:    envelope version, MULTICAST_PUBSUB_MESSAGE_VERSION
 * type: message type, receivers join the group of the types they want
 * src:  name of the sending device
 * seq:  per type sequence number, lets receivers detect lost messages
 * data: message specific JSON object
 */

/**
 * @brief Version of the message envelope
 *
 */
#define MULTICAST_PUBSUB_MESSAGE_VERSION (1)

/**
 * @brief Maximum size of a message, including the envelope
 *
 */
#define MULTICAST_PUBSUB_MAX_MESSAGE_SIZE (512)

/**
 * @brief Maximum number of subscriptions per handle
 *
 */
#define MULTICAST_PUBSUB_MAX_SUBSCRIPTIONS (8)

/**
 * @brief Multicast pubsub runtime structure
 *
 */
typedef struct multicast_pubsub multicast_pubsub_handle_t;

/**
 * @brief Configuration of multicast pubsub
 *
 */
typedef struct {
    const char *source; /*!< Name of this device, sent in every message, NULL if only subscribing */
    uint8_t ttl;        /*!< Multicast TTL, 1 keeps messages on the local network */
} multicast_pubsub_config_t;

/**
 * @brief A message type and the multicast group it is sent to
 *
 */
typedef struct {
    const char *type;  /*!< Message type, e.g. "air_quality" */
    const char *group; /*!< IPv4 multicast group address, e.g. "239.255.10.1" */
    uint16_t port;     /*!< UDP port */
    uint32_t seq;      /*!< Sequence number of the next message, updated by multicast_pubsub_publish() */
} multicast_pubsub_topic_t;

/**
 * @brief A received message, only valid during the callback
 *
 */
typedef struct {
    const char *type;   /*!< Message type */
    const char *source; /*!< Name of the sending device */
    uint32_t seq;       /*!< Sequence number */
    const cJSON *data;  /*!< Message data */
    const char *sender; /*!< IP address of the sending device */
} multicast_pubsub_message_t;

/**
 * @brief Called for each received message of a subscribed topic
 *
 * @param message Received message
 * @param ctx Context given to multicast_pubsub_subscribe()
 */
typedef void (*multicast_pubsub_callback_t)(const multicast_pubsub_message_t *message, void *ctx);

/**
 * @brief Check that a topic has a valid type and multicast group
 *
 * Publishing and subscribing check the topic too, but call this at startup to find a configuration
 * error before the first message.
 *
 * @param topic Topic to check
 * @return ESP_OK if the topic is valid
 */
esp_err_t multicast_pubsub_check_topic(const multicast_pubsub_topic_t *topic);

/**
 * @brief Init multicast pubsub
 *
 * The network stack must be initialized before calling this.
 *
 * @param config Configuration of multicast pubsub
 * @return multicast_pubsub_handle_t handle of multicast pubsub, NULL on failure
 */
multicast_pubsub_handle_t *multicast_pubsub_init(const multicast_pubsub_config_t *config);

/**
 * @brief Publish a message to the multicast group of a topic
 *
 * Not safe to call for the same topic from several tasks at once.
 *
 * @param handle Handle of multicast pubsub
 * @param topic Topic to publish to, its sequence number is incremented
 * @param data JSON object with the message data, e.g. "{\"pm25\":5}"
 * @return ESP_OK if the message was sent
 */
esp_err_t multicast_pubsub_publish(multicast_pubsub_handle_t *handle, multicast_pubsub_topic_t *topic, const char *data);

/**
 * @brief Join the multicast group of a topic and receive its messages
 *
 * Callbacks are called from a receive task that is created by the first subscription. Messages that
 * are not valid JSON, have another envelope version or another type than the topic are dropped.
 *
 * @param handle Handle of multicast pubsub
 * @param topic Topic to subscribe to
 * @param callback Called for each received message
 * @param ctx Passed to the callback
 * @return ESP_OK if subscribed
 */
esp_err_t multicast_pubsub_subscribe(multicast_pubsub_handle_t *handle, const multicast_pubsub_topic_t *topic,
                                     multicast_pubsub_callback_t callback, void *ctx);
