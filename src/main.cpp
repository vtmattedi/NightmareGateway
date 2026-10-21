#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "LedController/ledController.h"
#include "EspWifi/espWifi.h"
#include "TimeSync/timeSync.h"
#include <Gateway/NightMareGateway.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "creds.h"
#include "brokerCreds.h"
#include "esp_system.h"
#include "System/chipTemperature.h"
#include <Core/Config.h>
#include <Core/Logs.h>
#include <Core/NightMareCommand.h>
#include <Plataform/ESP32/NightMareESP.h>

#ifndef NM_ESPNOW_PSK
#error "Define NM_ESPNOW_PSK (the ESP-NOW network key, same on every device) in include/creds.h"
#endif

void loop();

#define STATUS_BRIGHTNESS 20

// Status LED: green bridged, blue MQTT only, pink ESP-NOW only, red neither.
static void showGatewayLink(NightMareGatewayLink link)
{
    switch (link)
    {
    case NightMareGatewayLink::BRIDGED:
        ws2812_set_color(COLOR_GREEN);
        break;
    case NightMareGatewayLink::REMOTE:
        ws2812_set_color(COLOR_BLUE);
        break;
    case NightMareGatewayLink::LOCAL:
        ws2812_set_color(COLOR_PINK);
        break;
    case NightMareGatewayLink::NONE:
        ws2812_set_color(COLOR_RED);
        break;
    }
}

// The application owns where the gateway's configuration comes from; today
// that is creds.h. The gateway copies what it needs.
static NightMareGatewayConfig gatewayConfig()
{
    static const uint8_t psk[] = NM_ESPNOW_PSK;
    NightMareGatewayConfig config;
    config.espnowConfig.psk = psk;
    config.espnowConfig.pskLength = sizeof(psk) - 1; // not the terminator

    // The remote broker when creds.h names one, else the local one.
    NightMareMqttConfig &mqtt = config.mqttConfig;
#if defined(REMOTE_MQTT_URL) && defined(REMOTE_MQTT_PORT)
    mqtt.uplink = NightMareGatewayUplink::REMOTE;
    mqtt.host = REMOTE_MQTT_URL;
    mqtt.port = REMOTE_MQTT_PORT;
    mqtt.tls = true;
#ifdef ROOT_CA
    mqtt.caCertificate = ROOT_CA;
#endif
#ifdef MQTT_USER
    mqtt.username = MQTT_USER;
#endif
#ifdef MQTT_PASSWD
    mqtt.password = MQTT_PASSWD;
#endif
#elif defined(LOCAL_MQTT_HOST) && defined(LOCAL_MQTT_PORT)
    mqtt.uplink = NightMareGatewayUplink::LOCAL;
    mqtt.host = LOCAL_MQTT_HOST;
    mqtt.port = LOCAL_MQTT_PORT;
#ifdef GATEWAY_USER
    mqtt.username = GATEWAY_USER;
#endif
#ifdef GATEWAY_PASSWD
    mqtt.password = GATEWAY_PASSWD;
#endif
#else
#error "Define either REMOTE_MQTT_URL/REMOTE_MQTT_PORT or LOCAL_MQTT_HOST/LOCAL_MQTT_PORT in include/creds.h"
#endif

    config.radioReady = wifi_is_started;
    config.ipReady = wifi_is_connected;
    config.onTick = chipTempTick;
    config.onLinkChange = showGatewayLink;
    return config;
}

void initSequence()
{
    // Initialize the LED controller
    ws2812_init();
    // Set initial color and brightness
    ws2812_set_color(COLOR_WHITE);
    ws2812_set_brightness(20);
    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for 1 second
    ws2812_set_color(COLOR_RED);
    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for 1 second
    ws2812_set_color(COLOR_GREEN);
    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for 1 second
    ws2812_set_color(COLOR_BLUE);
    vTaskDelay(pdMS_TO_TICKS(1000)); // Wait for 1 second
    ws2812_set_color(COLOR_BLACK);
}

static const gpio_num_t BLINK_GPIO = GPIO_NUM_8;
static const gpio_num_t BLINK_GPIO2 = GPIO_NUM_15;


// --- Serial commands ---------------------------------------------------------
// NightMare's serial console (NM_CONSOLE_SERIAL) reads the UART: its built-ins
// cover the framework and the gateway (GATEWAY ..., CONFIG ..., ADOPT,
// REBOOT, > resources). What only this application has is answered here, for
// any command the built-ins do not take:
//
//   WIFI SCAN                     networks in range, strongest first
//   WIFI CONNECT <ssid> [pass]    join another network (not saved)
//   HEAP                          free / minimum / largest free block
//   TEMP                          chip temperature now
//
// The periodic temperature log is the Config "chip:temperature_log":
// CONFIG SET chip:temperature_log false.

// The once-a-second chip temperature log in app_main; persisted.
static Config<bool> s_tempLog("chip:temperature_log", true);

static NightMareResults answer(bool ok, const std::string &text)
{
    NightMareResults result;
    result.result = ok;
    result.response = text;
    return result;
}

static std::string format(const char *fmt, ...)
{
    char buffer[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

static NightMareResults cmdHeap()
{
    return answer(true, format("heap free=%lu min=%lu bytes\ninternal free=%lu min=%lu largest=%lu bytes",
                               (unsigned long)esp_get_free_heap_size(),
                               (unsigned long)esp_get_minimum_free_heap_size(),
                               (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                               (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                               (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

static NightMareResults cmdTemp()
{
    float tempC;
    if (!chipTempRead(tempC))
        return answer(false, "chip temperature unavailable");
    return answer(true, format("chip temperature %.1f C", tempC));
}

static const char *authModeName(wifi_auth_mode_t mode)
{
    switch (mode)
    {
    case WIFI_AUTH_OPEN:
        return "open";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return "WPA2-ent";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3";
    case WIFI_AUTH_OWE:
        return "OWE";
    default:
        return "other";
    }
}

// [=======   ] for -55 dBm: full at -50 and better, empty at -100.
static std::string signalBar(int rssi)
{
    const int width = 10;
    int quality = (rssi + 100) * 2;
    quality = quality < 0 ? 0 : quality > 100 ? 100
                                              : quality;
    const int filled = (quality * width + 50) / 100;
    std::string bar = "[";
    for (int i = 0; i < width; i++)
        bar += i < filled ? '=' : ' ';
    return bar + "]";
}

// Blocks for the scan (about 2 s), on whichever task runs the console; ESP-NOW
// pauses for it either way.
static NightMareResults wifiScan()
{
    std::vector<wifi_ap_record_t> networks;
    const esp_err_t err = wifi_scan(networks);
    if (err != ESP_OK)
        return answer(false, format("Scan failed: %s", esp_err_to_name(err)));
    std::sort(networks.begin(), networks.end(),
              [](const wifi_ap_record_t &a, const wifi_ap_record_t &b)
              { return a.rssi > b.rssi; });

    wifi_ap_record_t current = {};
    const bool haveCurrent = wifi_is_connected() && esp_wifi_sta_get_ap_info(&current) == ESP_OK;

    std::string table = format(" %2s  %-12s  %8s  %3s  %-9s  %s\n", "#", "Signal", "dBm", "Ch", "Security", "SSID");
    table += " --  ------------  --------  ---  ---------  ------------------------\n";
    int index = 1;
    for (const wifi_ap_record_t &ap : networks)
    {
        const bool joined = haveCurrent && memcmp(ap.bssid, current.bssid, sizeof(ap.bssid)) == 0;
        const char *ssid = ap.ssid[0] != '\0' ? (const char *)ap.ssid : "<hidden>";
        table += format(" %2d  %s  %4d dBm  %3u  %-9s  %s%s\n", index++, signalBar(ap.rssi).c_str(), ap.rssi,
                        (unsigned)ap.primary, authModeName(ap.authmode), ssid, joined ? "   <- connected" : "");
    }
    table += format("%u network(s) found", (unsigned)networks.size());
    return answer(true, table);
}

// args[1] and args[2] keep their case; quote values with spaces.
static NightMareResults wifiConnect(const NightMareMessage &message)
{
    if (message.argc < 2 || message.argc > 3)
        return answer(false, "usage: WIFI CONNECT <ssid> [password]   (quote values with spaces)");
    const std::string &ssid = message.args[1];
    const std::string password = message.argc == 3 ? message.args[2] : std::string();
    const esp_err_t err = wifi_connect(ssid.c_str(), password.c_str());
    if (err == ESP_ERR_INVALID_ARG)
        return answer(false, "Invalid ssid (1-32 chars) or password (empty for open, else 8-63 chars)");
    if (err != ESP_OK)
        return answer(false, format("Could not start connecting: %s", esp_err_to_name(err)));
    return answer(true, format("Connecting to \"%s\"%s - the log shows the result", ssid.c_str(),
                               password.empty() ? " (open network)" : ""));
}

static NightMareResults resolveAppCommand(const NightMareMessage &message)
{
    if (message.command == "WIFI" && message.subcommand == "SCAN")
        return wifiScan();
    if (message.command == "WIFI" && message.subcommand == "CONNECT")
        return wifiConnect(message);
    if (message.command == "WIFI")
        return answer(false, "usage: WIFI SCAN | WIFI CONNECT <ssid> [password]");
    if (message.command == "HEAP")
        return cmdHeap();
    if (message.command == "TEMP")
        return cmdTemp();
    return answer(false, "Unknown command: " + message.command +
                             " (app: WIFI, HEAP, TEMP; gateway: GATEWAY GET; framework: CONFIG LIST, TIME, PING)");
}

static const char *TEMPTAG = "chip_temp";

uint64_t millis()
{
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

extern "C" void app_main(void)
{
    // NightMare, the gateway and this application log through NightMare's
    // logger (NM_LOG_LEVEL in NightMareConfig.h). What still uses ESP's --
    // ESP-IDF itself -- only says what is wrong.
    esp_log_level_set("*", ESP_LOG_WARN);

    // Identity and persisted Configs, before anything reads them; the gateway
    // binds the Resources when it starts.
    introNightMareESP();
    wifi_init_sta();
    ntp_sync_start();
    // NightMare's serial console takes what its built-ins and the gateway's
    // do not; it installs the console UART driver itself.
    setCommandResolver(resolveAppCommand);

    ws2812_init();
    ws2812_set_brightness(STATUS_BRIGHTNESS);
    // Before the gateway starts: it binds the temperature Resource.
    bool tempOk = chipTempInit();
    if (!tempOk)
        ESP_LOGE(TEMPTAG, "Failed to initialize chip temperature sensor");
    if (!NightMareGateway_init(gatewayConfig()))
        ESP_LOGE("main", "Gateway did not start: check its configuration");

    int lastTempLog = 0;
    while (1)
    {
        loop(); // big ol' arduino loop. just better for reasoning about the code.
        // A real sleep, not taskYIELD(): this task never blocks on its own, so
        // yield alone just hands control straight back with nothing else at
        // this priority to run, starving IDLE0 and tripping its watchdog.

        if (millis() - lastTempLog >= 1000) // Log every 1000 ticks (1 second)
        {
            lastTempLog = millis();
            float tempC;
            if (!s_tempLog.value())
                ; // silenced with CONFIG SET chip:temperature_log false
            else if (tempOk && chipTempRead(tempC))
                LOG(TEMPTAG, "Chip temperature: %.1f C", tempC);
            else if (tempOk)
                LOG_WARNING(TEMPTAG, "Failed to read chip temperature");
        }
        vTaskDelay(100); // very small delay
    }
}

void loop()
{
    // Returns at once with NM_GATEWAY_OWN_TASK (the gateway's own task runs
    // it, serial console included); with it off, this loop is what runs the
    // gateway.
    NightMareGateway_tick();
}
