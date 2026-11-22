#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "LedController/ledController.h"
#include "EspWifi/espmDNS.h"
#include "webServer/webserver.h"
#include "nvs_flash.h"
#include <Network/NmConnection.h>
#include <Gateway/NightMareGateway.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "creds.h"
#include "brokerCreds.h"
#include "esp_system.h"
#include "System/chipTemperature.h"
#include "System/memoryStats.h"
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

    // Wi-Fi is the library's (NM_ENABLE_WIFI): radioReady/ipReady default to it.
    config.onTick = []() {
        chipTempTick();
        memoryTick();
    };
    config.onLinkChange = showGatewayLink;
    return config;
}

// The web dashboard and mDNS live as long as the station has an address. Runs
// on a network task: start and stop only, nothing that blocks for long.
static void onNetworkStatus(const NightMare::NetworkStatusEvent &event)
{
    if (event.layer != NightMare::NetworkLayer::WIFI_IP)
        return;
    if (event.up)
    {
        NightmareGateway::mDNS::begin("nightmare-gateway");
        const esp_err_t err = web_server::start();
        if (err != ESP_OK)
            ESP_LOGE("main", "Failed to start web server: %s", esp_err_to_name(err));
    }
    else if (event.previous == NightMare::ConnectivityState::CONNECTED)
    {
        web_server::stop();
        NightmareGateway::mDNS::end();
    }
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
    const MemoryUsage m = memoryUsage();
    std::string text = format("internal free=%u/%u bytes (%.1f%%) min=%lu largest=%lu",
                              m.internalFree, m.internalTotal, m.internalFreePct(),
                              (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                              (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (m.psramTotal != 0)
        text += format("\npsram    free=%u/%u bytes (%.1f%%) min=%lu largest=%lu", m.psramFree, m.psramTotal,
                       m.psramFreePct(), (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
                       (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    return answer(true, text);
}

static NightMareResults cmdTemp()
{
    float tempC;
    if (!chipTempRead(tempC))
        return answer(false, "chip temperature unavailable");
    return answer(true, format("chip temperature %.1f C", tempC));
}

static NightMareResults resolveAppCommand(const NightMareMessage &message)
{
    if (message.command == "HEAP")
        return cmdHeap();
    if (message.command == "TEMP")
        return cmdTemp();
    return answer(false, "Unknown command: " + message.command +
                             " (app: HEAP, TEMP; gateway: GATEWAY GET; framework: NETWORK WIFI SCAN, CONFIG LIST, TIME, PING)");
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
    // The Wi-Fi driver keeps its calibration and settings in NVS.
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }
    introNightMareESP();
    // The library starts the radio, the station (saved profile, else creds.h)
    // and SNTP from the gateway task; the dashboard follows the IP link.
    NightMare::OnNetworkStatus(onNetworkStatus);
    // NightMare's serial console takes what its built-ins and the gateway's
    // do not; it installs the console UART driver itself.
    setCommandResolver(resolveAppCommand);

    ws2812_init();
    ws2812_set_brightness(STATUS_BRIGHTNESS);
    // Before the gateway starts: it binds the temperature Resource.
    memoryInit();
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
