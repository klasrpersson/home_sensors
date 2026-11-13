# The server certificate, its key and the token are in keys/ at the repository root, which git ignores.
# tools/generate_ota_keys.sh creates them. Only checked in the real build, as the early expansion runs for
# every component, also in firmwares that do not use this one.
get_filename_component(keys "${CMAKE_CURRENT_LIST_DIR}/../../keys" ABSOLUTE)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    foreach(key ota_server_cert.pem ota_server_key.pem ota_token)
        if(NOT EXISTS "${keys}/${key}")
            message(FATAL_ERROR "OTA key ${keys}/${key} not found. Run tools/generate_ota_keys.sh to create the keys.")
        endif()
    endforeach()
endif()

idf_component_register(SRCS "ota_server.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_https_server app_update esp_app_format bootloader_support esp_timer
                       EMBED_TXTFILES "${keys}/ota_server_cert.pem" "${keys}/ota_server_key.pem" "${keys}/ota_token")
