#include "wifi_manager.h"
#include <cstring>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "gateway_config.h"

namespace {
const char *TAG = "wifi";
EventGroupHandle_t events;
constexpr EventBits_t CONNECTED = BIT0;

void handler(void *, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Disconnected; reconnecting");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *e = static_cast<ip_event_got_ip_t *>(data);
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(events, CONNECTED);
    }
}
}

esp_err_t WifiManager::init() {
    if (!WIFI_SSID[0]) return ESP_ERR_INVALID_ARG;
    events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, handler, nullptr));

    wifi_config_t cfg = {};
    std::strncpy(reinterpret_cast<char *>(cfg.sta.ssid), WIFI_SSID, sizeof(cfg.sta.ssid)-1);
    std::strncpy(reinterpret_cast<char *>(cfg.sta.password), WIFI_PASSWORD, sizeof(cfg.sta.password)-1);
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(events, CONNECTED, pdFALSE, pdTRUE, portMAX_DELAY);
    return ESP_OK;
}
