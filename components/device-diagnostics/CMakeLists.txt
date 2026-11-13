idf_component_register(SRCS "device_diagnostics.c"
                       INCLUDE_DIRS "include"
                       # Public, the header uses the multicast pubsub handle and topic
                       REQUIRES multicast-pubsub
                       PRIV_REQUIRES esp_wifi esp_timer)
