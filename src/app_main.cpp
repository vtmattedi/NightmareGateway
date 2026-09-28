#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_manager.h"
#include "gateway_router.h"
#include "espnow_transport.h"
#include "mqtt_transport.h"

extern "C" void app_main(void) {
    esp_err_t err=nvs_flash_init();
    if(err==ESP_ERR_NVS_NO_FREE_PAGES||err==ESP_ERR_NVS_NEW_VERSION_FOUND){ESP_ERROR_CHECK(nvs_flash_erase());ESP_ERROR_CHECK(nvs_flash_init());}else ESP_ERROR_CHECK(err);
    ESP_LOGI("gateway","MattediWorks presents: NightMare Network Gateway");
    GatewayRouter::init();
    ESP_ERROR_CHECK(WifiManager::init());
    ESP_ERROR_CHECK(EspNowTransport::init());
    ESP_ERROR_CHECK(MqttTransport::init());
    ESP_LOGI("gateway","ready");
}
