#pragma once

// NightMare Network features used by the gateway, and the gateway's own
// (NightmareGateway/GatewayFeatures.h).
//
// The gateway is the broker (ESP-NOW side + its own MQTT uplink client) and a
// NightMare device like any other: its transport is the GATEWAY connection,
// so status, info, telemetry, Resources, console, jobs, the Wi-Fi station and
// SNTP all come from NightMare Network.

#define NM_FIRMWARE_VERSION "nightmare-gateway"

// One logger for NightMare, the gateway and the app's own events: info shows
// network events, warnings and errors; LOG_DEBUG chatter compiles out. Lines
// carry the local time once SNTP has set the clock, uptime ms before that.
#define NM_LOG_LEVEL NM_LOG_LEVEL_INFO
#define NM_LOG_USE_TIME_OF_DAY 1

#define NM_ENABLE_SETTINGS 1
#define NM_ENABLE_RESOURCES 1
#define NM_ENABLE_CONSOLE 1
#define NM_CONSOLE_BUILTINS 1
// NightMare's serial console reads the UART (it installs the console driver);
// the application adds HEAP and TEMP through setCommandResolver().
#define NM_CONSOLE_SERIAL 1

#ifndef NM_ENABLE_GATEWAY
#define NM_ENABLE_GATEWAY 1
#endif
#ifndef NM_GATEWAY_ESPNOW
#define NM_GATEWAY_ESPNOW 1
#endif
#ifndef NM_GATEWAY_MQTT
#define NM_GATEWAY_MQTT 1
#endif
// 1: the gateway runs on its own task; 0: main's loop runs it through
// NightMareGateway_tick().
#ifndef NM_GATEWAY_OWN_TASK
#define NM_GATEWAY_OWN_TASK 1
#endif
// Devices reconnecting together (after a gateway reboot they all come back at
// once) each send a 20+ fragment hardware manifest; a 32-slot queue lost
// frames. The slots live in PSRAM (260 B each, ~65 KB at 256), so there is no
// reason to be tight. The MQTT queue holds pointers and also has to hold what
// ESP-NOW forwards while the broker session is still connecting.
#define NM_GATEWAY_ESPNOW_RX_QUEUE 256
#define NM_GATEWAY_MQTT_OUTBOUND_QUEUE 128

// The device stack on the GATEWAY connection. The library's Wi-Fi station and
// SNTP run the gateway's Wi-Fi; its MQTT client stays off (the uplink is the
// gateway's own) and so does the ESP-NOW client (a gateway is not a client).
#define NM_ENABLE_NETWORK 1
#define NM_ENABLE_WIFI 1
#define NM_ENABLE_MQTT 0
#define NM_NETWORK_ESPNOW 0
#define NM_ENABLE_TIME_SYNC 1
#define NM_TIMEZONE "<-03>3"
#define NM_ENABLE_TELEMETRY 1
#define NM_ENABLE_SCHEDULER 1
// Resources may only be touched from the gateway's tick, so jobs run there
// (MANUAL), not in a task of their own.
#define NM_SCHEDULER_OWN_TASK 0
#define NM_ENABLE_JOBS 1

#define NM_ENABLE_OTA 0
#define NM_ENABLE_HTTP 0
#define NM_ENABLE_WEBSOCKET 0
#define NM_ENABLE_LVGL 0
