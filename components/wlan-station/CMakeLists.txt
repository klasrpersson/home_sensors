idf_component_register(SRCS "wlan_station.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer nvs_flash)

# The Wi-Fi network is set in wifi_credentials.h at the repository root, which git ignores, so it is not
# saved in sdkconfig, where idf.py save-defconfig would copy it into the tracked sdkconfig.defaults
get_filename_component(credentials "${COMPONENT_DIR}/../../wifi_credentials.h" ABSOLUTE)
if(NOT EXISTS "${credentials}")
    message(FATAL_ERROR "Wi-Fi network not set. Copy wifi_credentials.example.h at the repository root "
                        "to wifi_credentials.h and set your network in it.")
endif()
target_compile_definitions(${COMPONENT_LIB} PUBLIC "WLAN_STATION_CREDENTIALS=\"${credentials}\"")
