#include "peer_registry.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "gateway_config.h"

namespace { const char *TAG="peers"; }

void PeerRegistry::touch(const MacAddress &mac, uint32_t now) {
    for (auto &p : peers_) if (p.used && p.mac == mac) { p.lastSeen = now; return; }
    for (auto &p : peers_) {
        if (p.used) continue;
        p.used=true; p.mac=mac; p.lastSeen=now;
        if (!esp_now_is_peer_exist(mac.bytes)) {
            esp_now_peer_info_t info = {};
            for (int i=0;i<6;++i) info.peer_addr[i]=mac.bytes[i];
            info.ifidx=WIFI_IF_STA;
            info.encrypt=false;
            esp_err_t err=esp_now_add_peer(&info);
            if (err!=ESP_OK && err!=ESP_ERR_ESPNOW_EXIST) ESP_LOGW(TAG,"add peer: %s",esp_err_to_name(err));
        }
        ESP_LOGI(TAG,"peer %02X:%02X:%02X:%02X:%02X:%02X",mac.bytes[0],mac.bytes[1],mac.bytes[2],mac.bytes[3],mac.bytes[4],mac.bytes[5]);
        return;
    }
    ESP_LOGW(TAG,"peer registry full");
}

void PeerRegistry::expire(uint32_t now) {
    for (auto &p : peers_) if (p.used && (now-p.lastSeen)>NM_PEER_TIMEOUT_MS) { esp_now_del_peer(p.mac.bytes); p=Peer{}; }
}
