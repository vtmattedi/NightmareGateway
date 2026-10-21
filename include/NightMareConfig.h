#pragma once

// NightMare Network features used by the gateway, and the gateway's own
// (NightmareGateway/GatewayFeatures.h).
//
// The gateway is the broker: it owns its Wi-Fi station, its MQTT uplink and the
// ESP-NOW side, so NightMare's own connectivity stack stays off. What it takes
// from NightMare is the device plumbing -- identity, persistent Config, the
// Resource model and the command surface. Its own Resources travel over the
// gateway's transport, injected in Gateway/GatewayNode (NightMare Network).

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
// the application adds WIFI, HEAP and TEMP through setCommandResolver().
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

// Gateway-owned: Wi-Fi, MQTT, ESP-NOW and SNTP.
#define NM_ENABLE_NETWORK 0
#define NM_ENABLE_WIFI 0
#define NM_ENABLE_MQTT 0
#define NM_NETWORK_ESPNOW 0
#define NM_ENABLE_TIME_SYNC 0
// Telemetry publishes through NmConnection, which is off.
#define NM_ENABLE_TELEMETRY 0
// Jobs would run on the Scheduler task, and Resources may only be touched from
// the gateway's tick.
#define NM_ENABLE_SCHEDULER 0
#define NM_ENABLE_JOBS 0

#define NM_ENABLE_OTA 0
#define NM_ENABLE_HTTP 0
#define NM_ENABLE_WEBSOCKET 0
#define NM_ENABLE_LVGL 0
