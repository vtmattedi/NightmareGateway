#pragma once
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"

constexpr const char *GATEWAY_NAME = "nmnw-gateway";

constexpr const char *WIFI_SSID = "";
constexpr const char *WIFI_PASSWORD = "";

constexpr const char *MQTT_BROKER_URI = "mqtt://192.168.1.10:1883";
constexpr const char *MQTT_USERNAME = "";
constexpr const char *MQTT_PASSWORD = "";
constexpr const char *MQTT_SUBSCRIPTION = "#";

constexpr uint8_t NM_ESPNOW_PROTOCOL_VERSION = 1;
constexpr size_t NM_ESPNOW_MAX_FRAME_BYTES = 230;
constexpr size_t NM_MAX_TOPIC_BYTES = 192;
constexpr size_t NM_MAX_PAYLOAD_BYTES = 32768;
constexpr size_t NM_MAX_ESPNOW_PEERS = 32;
constexpr uint32_t NM_PEER_TIMEOUT_MS = 120000;
constexpr uint32_t NM_HELLO_INTERVAL_MS = 10000;
constexpr size_t NM_MAX_REASSEMBLY_SLOTS = 8;
constexpr uint32_t NM_REASSEMBLY_TIMEOUT_MS = 5000;
constexpr size_t NM_RECENT_MESSAGE_CACHE_SIZE = 64;
constexpr uint32_t NM_RECENT_MESSAGE_TTL_MS = 10000;
constexpr uint32_t ROUTER_QUEUE_DEPTH = 12;
constexpr uint32_t ROUTER_TASK_STACK = 8192;
constexpr UBaseType_t ROUTER_TASK_PRIORITY = 5;
