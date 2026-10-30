#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "wlan_station.h"

static const char *TAG = "wlan_station";

/**
 * @brief Wi-Fi and IP event handler, keeps the station connected
 *
 */
static void wlan_station_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *) event_data;
        ESP_LOGW(TAG, "Disconnected, reason %d, reconnecting", event->reason);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Connected, got ip " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

/**
 * @brief Start Wi-Fi and connect to a network
 *
 * @param config Configuration of Wi-Fi station
 * @return ESP_OK if Wi-Fi was started
 */
esp_err_t wlan_station_start(const wlan_station_config_t *config) {
    wifi_config_t wifi_config = {
            .sta = {
                    .sae_pwe_h2e = WPA3_SAE_PWE_BOTH
            }
    };

    size_t ssid_len = strlen(config->ssid);
    size_t password_len = strlen(config->password);
    if (ssid_len == 0) {
        ESP_LOGE(TAG, "Wi-Fi SSID is not set");
        return ESP_ERR_INVALID_ARG;
    }
    if (ssid_len > sizeof(wifi_config.sta.ssid) || password_len > sizeof(wifi_config.sta.password)) {
        ESP_LOGE(TAG, "Wi-Fi SSID or password is too long");
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(wifi_config.sta.ssid, config->ssid, ssid_len);
    memcpy(wifi_config.sta.password, config->password, password_len);
    wifi_config.sta.threshold.authmode = password_len == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS failed");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "init NVS failed");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "init network stack failed");
    err = esp_event_loop_create_default();
    if (err != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(err, TAG, "create default event loop failed");
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        ESP_LOGE(TAG, "create Wi-Fi station interface failed");
        return ESP_FAIL;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "init Wi-Fi failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wlan_station_event_handler, NULL, NULL),
                        TAG, "register Wi-Fi event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wlan_station_event_handler, NULL, NULL),
                        TAG, "register IP event handler failed");

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set Wi-Fi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "set Wi-Fi config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start Wi-Fi failed");

    ESP_LOGI(TAG, "Connecting to %s", config->ssid);

    return ESP_OK;
}
