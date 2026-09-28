idf_component_register(
    SRCS
        "app_main.cpp"
        "wifi_manager.cpp"
        "mqtt_transport.cpp"
        "espnow_transport.cpp"
        "gateway_router.cpp"
        "peer_registry.cpp"
        "fragment_reassembler.cpp"
        "recent_message_cache.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash mqtt
)
