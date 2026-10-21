#include "espnow_transport.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "fragment_reassembler.h"
#include "gateway_config.h"
#include "gateway_router.h"
#include "peer_registry.h"
#include "recent_message_cache.h"

namespace {
PeerRegistry peers; FragmentReassembler reasm; RecentMessageCache recent; std::atomic<uint32_t> nextId{1};
uint32_t nowMs(){return (uint32_t)(esp_timer_get_time()/1000ULL);} MacAddress macOf(const uint8_t *m){MacAddress x{}; if(m)std::memcpy(x.bytes,m,6); return x;}

void recv_cb(const esp_now_recv_info_t *info,const uint8_t *data,int len){
    if(!info||!data||len<(int)sizeof(EspNowFrameHeader)) return;
    uint32_t now=nowMs(); MacAddress source=macOf(info->src_addr); peers.touch(source,now);
    EspNowFrameHeader h{}; std::memcpy(&h,data,sizeof(h));
    if(h.version!=NM_ESPNOW_PROTOCOL_VERSION) return;
    if(h.type==(uint8_t)EspNowFrameType::Hello) return;
    if(h.type!=(uint8_t)EspNowFrameType::Message) return;
    if(recent.seen(source,h.messageId,now)) return;
    GatewayMessage m; if(!reasm.ingest(source,data,(size_t)len,now,m)) return;
    recent.remember(source,m.espnowMessageId,now); GatewayRouter::enqueue(m);
}

bool send_peer(const MacAddress &dst,const GatewayMessage &m){
    if(m.topic.size()>NM_MAX_TOPIC_BYTES || m.payload.size()>NM_MAX_PAYLOAD_BYTES) return false;
    std::vector<uint8_t> logical; logical.reserve(m.topic.size()+m.payload.size()); logical.insert(logical.end(),m.topic.begin(),m.topic.end()); logical.insert(logical.end(),m.payload.begin(),m.payload.end());
    constexpr size_t cap=NM_ESPNOW_MAX_FRAME_BYTES-sizeof(EspNowFrameHeader); if(!cap) return false;
    size_t count=std::max<size_t>(1,(logical.size()+cap-1)/cap); if(count>255) return false;
    uint32_t id=nextId.fetch_add(1);
    for(size_t i=0;i<count;++i){
        size_t off=i*cap, n=std::min(cap,logical.size()-off);
        EspNowFrameHeader h{}; h.version=NM_ESPNOW_PROTOCOL_VERSION; h.type=(uint8_t)EspNowFrameType::Message; h.flags=m.retained?ESPNOW_FLAG_RETAINED:0; h.fragmentIndex=(uint8_t)i; h.fragmentCount=(uint16_t)count; h.topicLength=(uint16_t)m.topic.size(); h.payloadLength=(uint32_t)m.payload.size(); h.messageId=id;
        std::vector<uint8_t> frame(sizeof(h)+n); std::memcpy(frame.data(),&h,sizeof(h)); if(n)std::memcpy(frame.data()+sizeof(h),logical.data()+off,n);
        if(esp_now_send(dst.bytes,frame.data(),frame.size())!=ESP_OK) return false;
    }
    return true;
}
}

esp_err_t EspNowTransport::init(){
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wifiInit=WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifiInit));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(NM_ESPNOW_CHANNEL,WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(recv_cb));
    return ESP_OK;
}
bool EspNowTransport::sendToAllExcept(const GatewayMessage &m,const MacAddress *excluded){bool ok=true; peers.forEach([&](const MacAddress &p,uint32_t){if(excluded&&p==*excluded)return; if(!send_peer(p,m))ok=false;}); return ok;}
void EspNowTransport::logDevices(){
    uint32_t now=nowMs(); int count=0;
    peers.forEach([&](const MacAddress &p,uint32_t lastSeen){
        ++count;
        ESP_LOGI("peers","%02X:%02X:%02X:%02X:%02X:%02X last_seen=%lums ago",p.bytes[0],p.bytes[1],p.bytes[2],p.bytes[3],p.bytes[4],p.bytes[5],(unsigned long)(now-lastSeen));
    });
    if(!count) ESP_LOGI("peers","no known devices");
}
