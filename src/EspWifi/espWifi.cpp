#include "espWifi.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "webServer/webserver.h"
#include <Core/Logs.h>
#include <atomic>

static const char *TAG = "espWifi";
static EventGroupHandle_t wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_MAX_RETRY 5

static int retry_count = 0;
static std::atomic<bool> s_connected{false};
static std::atomic<bool> s_started{false};
static std::atomic<int> s_lastReason{0}; // reason of the latest STA_DISCONNECTED, retries included

// The IDF has no name table for disconnect reasons; these are the ones that
// show up in practice. Anything else prints as a number only.
static const char *reasonName(int reason)
{
    switch (reason)
    {
    case WIFI_REASON_UNSPECIFIED: return "unspecified";
    case WIFI_REASON_AUTH_EXPIRE: return "auth expired";
    case WIFI_REASON_AUTH_LEAVE: return "AP deauthenticated us";
    case WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY: return "AP dropped us for inactivity";
    case WIFI_REASON_ASSOC_TOOMANY: return "AP has too many clients";
    case WIFI_REASON_ASSOC_LEAVE: return "AP disassociated us";
    case WIFI_REASON_MIC_FAILURE: return "MIC failure";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4-way handshake timeout (wrong password?)";
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "group key update timeout";
    case WIFI_REASON_802_1X_AUTH_FAILED: return "802.1X auth failed";
    case WIFI_REASON_MISSING_ACKS: return "missing ACKs";
    case WIFI_REASON_TIMEOUT: return "timeout";
    case WIFI_REASON_PEER_INITIATED: return "AP initiated disassociation";
    case WIFI_REASON_AP_INITIATED: return "AP initiated disassociation";
    case WIFI_REASON_BEACON_TIMEOUT: return "beacon timeout (AP went out of range or silent)";
    case WIFI_REASON_NO_AP_FOUND: return "AP not found (wrong SSID, out of range or hidden?)";
    case WIFI_REASON_AUTH_FAIL: return "authentication failed (wrong password?)";
    case WIFI_REASON_ASSOC_FAIL: return "association failed";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "handshake timeout (wrong password?)";
    case WIFI_REASON_CONNECTION_FAIL: return "connection failed";
    case WIFI_REASON_AP_TSF_RESET: return "AP restarted";
    case WIFI_REASON_ROAMING: return "roaming";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY: return "AP found but security does not match";
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return "AP found but below the required auth mode";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return "AP found but signal too weak";
    default: return "see wifi_err_reason_t";
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        const wifi_event_sta_disconnected_t *lost = (const wifi_event_sta_disconnected_t *)event_data;
        const int reason = lost != nullptr ? (int)lost->reason : -1;
        s_lastReason.store(reason, std::memory_order_relaxed);
        // Only the drop from connected is an event; the retries that follow are not.
        if (s_connected.exchange(false, std::memory_order_acq_rel))
            LOG("wifi", "wifi disconnected (reason %d: %s)", reason, reasonName(reason));
        web_server::stop();
        if (retry_count < WIFI_MAX_RETRY)
        {
            esp_wifi_connect();
            retry_count++;
            ESP_LOGW(TAG, "Connection to AP failed (reason %d: %s), retry %d/%d", reason, reasonName(reason),
                     retry_count, WIFI_MAX_RETRY);
        }
        else
        {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            NightmareGateway::mDNS::end();
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        wifi_ap_record_t ap = {};
        esp_wifi_sta_get_ap_info(&ap);
        LOG("wifi", "wifi connected: %s ip " IPSTR, (const char *)ap.ssid, IP2STR(&event->ip_info.ip));
        retry_count = 0;
        s_connected.store(true, std::memory_order_release);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        NightmareGateway::mDNS::begin("nightmare-gateway");
        esp_err_t web_server_result = web_server::start();
        if (web_server_result != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to start web server: %s",
                     esp_err_to_name(web_server_result));
        }
    }
}

bool wifi_is_connected(void)
{
    return s_connected.load(std::memory_order_acquire);
}

bool wifi_is_started(void)
{
    return s_started.load(std::memory_order_acquire);
}

esp_err_t wifi_scan(std::vector<wifi_ap_record_t> &out)
{
    if (!wifi_is_started())
        return ESP_ERR_WIFI_NOT_STARTED;

    wifi_scan_config_t config = {};
    config.show_hidden = true;
    esp_err_t err = esp_wifi_scan_start(&config, true);
    if (err != ESP_OK)
        return err;

    uint16_t count = 0;
    err = esp_wifi_scan_get_ap_num(&count);
    if (err != ESP_OK)
        return err;
    if (count > 40)
        count = 40; // the rest is discarded by get_ap_records
    out.assign(count, wifi_ap_record_t{});
    err = esp_wifi_scan_get_ap_records(&count, out.data());
    out.resize(err == ESP_OK ? count : 0);
    return err;
}

esp_err_t wifi_connect(const char *ssid, const char *password)
{
    if (!wifi_is_started())
        return ESP_ERR_WIFI_NOT_STARTED;
    if (password == nullptr)
        password = "";
    const size_t ssidLength = ssid != nullptr ? strlen(ssid) : 0;
    const size_t passwordLength = strlen(password);
    wifi_config_t config = {};
    if (ssidLength == 0 || ssidLength > sizeof(config.sta.ssid) ||
        passwordLength > 63 || (passwordLength != 0 && passwordLength < 8))
        return ESP_ERR_INVALID_ARG;

    memcpy(config.sta.ssid, ssid, ssidLength);
    memcpy(config.sta.password, password, passwordLength);
    config.sta.threshold.authmode = passwordLength == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err != ESP_OK)
        return err;

    // Leaving the old network raises DISCONNECTED, whose handler reconnects
    // with the new config. When nothing was connected there is no such event.
    const bool wasConnected = wifi_is_connected();
    retry_count = 0;
    esp_wifi_disconnect();
    if (!wasConnected)
        esp_wifi_connect();
    return ESP_OK;
}

void wifi_init_sta(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, DEFAULT_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, DEFAULT_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    // Routers in WPA2/WPA3 transition mode or with 802.11w on answer a station
    // that is not PMF-capable with auth timeouts instead of a clear rejection.
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    // 20 MHz is far more tolerant of a marginal or noisy link than HT40, and
    // the gateway needs no more throughput than that.
    ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_started.store(true, std::memory_order_release);

    ESP_LOGI(TAG, "Connecting to SSID: %s", DEFAULT_SSID);

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Connected to SSID: %s", DEFAULT_SSID);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        const int reason = s_lastReason.load(std::memory_order_relaxed);
        ESP_LOGE(TAG, "Failed to connect to SSID: %s (last reason %d: %s)", DEFAULT_SSID, reason, reasonName(reason));
    }
}
