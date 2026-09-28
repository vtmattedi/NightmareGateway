#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

FILES = {
"CMakeLists.txt": r'''cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nmnw_gateway)
''',
"sdkconfig.defaults": r'''CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_MQTT_PROTOCOL_5=y
''',
".gitignore": r'''build/
sdkconfig
sdkconfig.old
.vscode/
''',
"main/CMakeLists.txt": r'''idf_component_register(
    SRCS
        "app_main.cpp"
        "wifi_manager.cpp"
        "mqtt_transport.cpp"
        "espnow_transport.cpp"
        "gateway_router.cpp"
        "peer_registry.cpp"
        "fragment_reassembler.cpp"
        "recent_message_cache.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash mqtt
)
''',
"main/gateway_config.h": r'''#pragma once
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
''',
"main/gateway_types.h": r'''#pragma once
#include <stdint.h>
#include <string>
#include <vector>

enum class GatewayIngress : uint8_t { EspNow, Mqtt };

struct MacAddress {
    uint8_t bytes[6] = {0};
    bool operator==(const MacAddress &o) const {
        for (int i = 0; i < 6; ++i) if (bytes[i] != o.bytes[i]) return false;
        return true;
    }
};

struct GatewayMessage {
    GatewayIngress ingress = GatewayIngress::EspNow;
    MacAddress sourceMac{};
    std::string topic;
    std::vector<uint8_t> payload;
    bool retained = false;
    uint32_t espnowMessageId = 0;
};
''',
"main/wifi_manager.h": r'''#pragma once
#include "esp_err.h"
namespace WifiManager { esp_err_t init(); }
''',
"main/wifi_manager.cpp": r'''#include "wifi_manager.h"
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
''',
"main/recent_message_cache.h": r'''#pragma once
#include <stddef.h>
#include <stdint.h>
#include "gateway_types.h"
class RecentMessageCache {
public:
    bool seen(const MacAddress &, uint32_t, uint32_t) const;
    void remember(const MacAddress &, uint32_t, uint32_t);
private:
    struct Entry { bool used=false; MacAddress source{}; uint32_t id=0; uint32_t at=0; };
    Entry entries_[64]{};
    size_t next_=0;
};
''',
"main/recent_message_cache.cpp": r'''#include "recent_message_cache.h"
#include "gateway_config.h"
bool RecentMessageCache::seen(const MacAddress &s, uint32_t id, uint32_t now) const {
    for (const auto &e : entries_) if (e.used && e.source == s && e.id == id && (now-e.at) <= NM_RECENT_MESSAGE_TTL_MS) return true;
    return false;
}
void RecentMessageCache::remember(const MacAddress &s, uint32_t id, uint32_t now) {
    entries_[next_] = {true, s, id, now};
    next_ = (next_ + 1) % NM_RECENT_MESSAGE_CACHE_SIZE;
}
''',
"main/peer_registry.h": r'''#pragma once
#include <stdint.h>
#include "gateway_types.h"
class PeerRegistry {
public:
    void touch(const MacAddress &, uint32_t);
    void expire(uint32_t);
    template<class F> void forEach(F f) const { for (const auto &p : peers_) if (p.used) f(p.mac); }
private:
    struct Peer { bool used=false; MacAddress mac{}; uint32_t lastSeen=0; };
    Peer peers_[32]{};
};
''',
"main/peer_registry.cpp": r'''#include "peer_registry.h"
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
''',
"main/fragment_reassembler.h": r'''#pragma once
#include <stdint.h>
#include <vector>
#include "gateway_types.h"

struct EspNowFrameHeader {
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint8_t fragmentIndex;
    uint16_t fragmentCount;
    uint16_t topicLength;
    uint32_t payloadLength;
    uint32_t messageId;
} __attribute__((packed));

enum class EspNowFrameType : uint8_t { Hello=1, Message=2 };
constexpr uint8_t ESPNOW_FLAG_RETAINED = 0x01;

class FragmentReassembler {
public:
    bool ingest(const MacAddress &, const uint8_t *, size_t, uint32_t, GatewayMessage &);
    void expire(uint32_t);
private:
    struct Slot {
        bool used=false; MacAddress source{}; uint32_t id=0; uint32_t at=0; uint16_t count=0; uint16_t topicLen=0; uint32_t payloadLen=0; uint8_t flags=0;
        std::vector<std::vector<uint8_t>> fragments; std::vector<bool> received;
    };
    Slot slots_[8]{};
};
''',
"main/fragment_reassembler.cpp": r'''#include "fragment_reassembler.h"
#include <cstring>
#include "gateway_config.h"

bool FragmentReassembler::ingest(const MacAddress &source,const uint8_t *data,size_t len,uint32_t now,GatewayMessage &out) {
    if (len < sizeof(EspNowFrameHeader)) return false;
    EspNowFrameHeader h{}; std::memcpy(&h,data,sizeof(h));
    if (h.version!=NM_ESPNOW_PROTOCOL_VERSION || h.type!=(uint8_t)EspNowFrameType::Message || !h.fragmentCount || h.fragmentIndex>=h.fragmentCount) return false;
    if (h.topicLength>NM_MAX_TOPIC_BYTES || h.payloadLength>NM_MAX_PAYLOAD_BYTES) return false;

    Slot *s=nullptr;
    for (auto &x:slots_) if (x.used && x.source==source && x.id==h.messageId) { s=&x; break; }
    if (!s) for (auto &x:slots_) if (!x.used) { s=&x; x.used=true; x.source=source; x.id=h.messageId; x.count=h.fragmentCount; x.topicLen=h.topicLength; x.payloadLen=h.payloadLength; x.flags=h.flags; x.fragments.resize(h.fragmentCount); x.received.assign(h.fragmentCount,false); break; }
    if (!s) return false;
    s->at=now;
    const uint8_t *body=data+sizeof(EspNowFrameHeader); size_t bodyLen=len-sizeof(EspNowFrameHeader);
    s->fragments[h.fragmentIndex].assign(body,body+bodyLen); s->received[h.fragmentIndex]=true;
    for (bool r:s->received) if (!r) return false;

    std::vector<uint8_t> assembled;
    for (auto &f:s->fragments) assembled.insert(assembled.end(),f.begin(),f.end());
    size_t required=(size_t)s->topicLen+(size_t)s->payloadLen;
    if (assembled.size()<required) { *s=Slot{}; return false; }

    out=GatewayMessage{}; out.ingress=GatewayIngress::EspNow; out.sourceMac=source; out.espnowMessageId=s->id; out.retained=(s->flags&ESPNOW_FLAG_RETAINED)!=0;
    out.topic.assign((const char *)assembled.data(),s->topicLen);
    out.payload.assign(assembled.begin()+s->topicLen,assembled.begin()+required);
    *s=Slot{};
    return true;
}

void FragmentReassembler::expire(uint32_t now) { for (auto &s:slots_) if (s.used && (now-s.at)>NM_REASSEMBLY_TIMEOUT_MS) s=Slot{}; }
''',
"main/gateway_router.h": r'''#pragma once
#include "gateway_types.h"
namespace GatewayRouter { void init(); bool enqueue(const GatewayMessage &); }
''',
"main/mqtt_transport.h": r'''#pragma once
#include "esp_err.h"
#include "gateway_types.h"
namespace MqttTransport { esp_err_t init(); bool publish(const GatewayMessage &); }
''',
"main/espnow_transport.h": r'''#pragma once
#include "esp_err.h"
#include "gateway_types.h"
namespace EspNowTransport { esp_err_t init(); bool sendToAllExcept(const GatewayMessage &, const MacAddress *); }
''',
"main/gateway_router.cpp": r'''#include "gateway_router.h"
#include <memory>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "espnow_transport.h"
#include "gateway_config.h"
#include "mqtt_transport.h"

namespace {
QueueHandle_t q=nullptr;
void task(void *) {
    GatewayMessage *m=nullptr;
    for (;;) {
        if (xQueueReceive(q,&m,portMAX_DELAY)!=pdTRUE) continue;
        std::unique_ptr<GatewayMessage> owned(m);
        if (!m) continue;
        if (m->ingress==GatewayIngress::EspNow) {
            EspNowTransport::sendToAllExcept(*m,&m->sourceMac); // local first
            MqttTransport::publish(*m);
        } else {
            EspNowTransport::sendToAllExcept(*m,nullptr);
        }
    }
}
}
void GatewayRouter::init() { q=xQueueCreate(ROUTER_QUEUE_DEPTH,sizeof(GatewayMessage*)); configASSERT(q); configASSERT(xTaskCreate(task,"gateway_router",ROUTER_TASK_STACK,nullptr,ROUTER_TASK_PRIORITY,nullptr)==pdPASS); }
bool GatewayRouter::enqueue(const GatewayMessage &m) { auto *c=new(std::nothrow) GatewayMessage(m); if(!c) return false; if(xQueueSend(q,&c,0)!=pdTRUE){delete c; return false;} return true; }
''',
"main/mqtt_transport.cpp": r'''#include "mqtt_transport.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "gateway_config.h"
#include "gateway_router.h"

namespace {
const char *TAG="mqtt"; esp_mqtt_client_handle_t client=nullptr;
void subscribe_no_local() {
    esp_mqtt5_subscribe_property_config_t p={}; p.no_local_flag=true; p.retain_as_published_flag=true; p.retain_handle=0;
    ESP_ERROR_CHECK(esp_mqtt5_client_set_subscribe_property(client,&p));
    int id=esp_mqtt_client_subscribe(client,MQTT_SUBSCRIPTION,0);
    ESP_LOGI(TAG,"subscribe %s no-local=true msg=%d",MQTT_SUBSCRIPTION,id);
}
void handler(void *,esp_event_base_t,int32_t event_id,void *event_data) {
    auto *e=(esp_mqtt_event_handle_t)event_data;
    switch((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: subscribe_no_local(); break;
        case MQTT_EVENT_DATA: {
            GatewayMessage m; m.ingress=GatewayIngress::Mqtt; m.topic.assign(e->topic,e->topic_len); m.retained=e->retain!=0;
            if(e->data && e->data_len>0) m.payload.assign((uint8_t*)e->data,(uint8_t*)e->data+e->data_len);
            GatewayRouter::enqueue(m); break;
        }
        case MQTT_EVENT_ERROR: ESP_LOGE(TAG,"MQTT error"); break;
        default: break;
    }
}
}

esp_err_t MqttTransport::init() {
    esp_mqtt_client_config_t cfg={};
    cfg.broker.address.uri=MQTT_BROKER_URI;
    cfg.credentials.username=MQTT_USERNAME[0]?MQTT_USERNAME:nullptr;
    cfg.credentials.authentication.password=MQTT_PASSWORD[0]?MQTT_PASSWORD:nullptr;
    cfg.credentials.client_id=GATEWAY_NAME;
    cfg.session.protocol_ver=MQTT_PROTOCOL_V_5;
    client=esp_mqtt_client_init(&cfg); if(!client) return ESP_FAIL;
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client,ESP_EVENT_ANY_ID,handler,nullptr));
    return esp_mqtt_client_start(client);
}

bool MqttTransport::publish(const GatewayMessage &m) {
    if(!client) return false;
    int id=esp_mqtt_client_publish(client,m.topic.c_str(),(const char*)m.payload.data(),(int)m.payload.size(),0,m.retained?1:0);
    return id>=0;
}
''',
"main/espnow_transport.cpp": r'''#include "espnow_transport.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>
#include "esp_now.h"
#include "esp_timer.h"
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

esp_err_t EspNowTransport::init(){ESP_ERROR_CHECK(esp_now_init()); ESP_ERROR_CHECK(esp_now_register_recv_cb(recv_cb)); return ESP_OK;}
bool EspNowTransport::sendToAllExcept(const GatewayMessage &m,const MacAddress *excluded){bool ok=true; peers.forEach([&](const MacAddress &p){if(excluded&&p==*excluded)return; if(!send_peer(p,m))ok=false;}); return ok;}
''',
"main/app_main.cpp": r'''#include "esp_log.h"
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
''',
"README.md": r'''# NightMare Network Gateway v1

Target: generic ESP32-S3 N16R8, native ESP-IDF.

## Scope

```text
ESP-NOW IN
  -> distribute locally to all known peers except sender
  -> publish to MQTT

MQTT IN
  -> distribute to all known ESP-NOW peers
```

The gateway is protocol-transparent. It transports the current NightMare Network logical message:

```text
topic + payload + retained
```

It does not parse Resources, manifests, `/set`, `/invoke`, or other NMNW semantics.

## MQTT loop prevention

The MQTT client uses MQTT v5 and subscribes with `No Local = true`, so a compliant broker does not send the gateway's own publications back to the same client subscription.

This is the rule we want:

```text
ESP-NOW -> gateway -> MQTT
                    X
             no echo back
                    X
                 ESP-NOW
```

## ESP-NOW peer discovery

Any valid frame teaches the gateway the sender MAC. Endpoints should also send a periodic HELLO frame, recommended every 10 seconds, so peers are rediscovered after a gateway reboot.

The peer registry is RAM-only in v1.

## ESP-NOW transport

The transport-only frame header is:

```cpp
struct EspNowFrameHeader {
    uint8_t version;
    uint8_t type;          // HELLO or MESSAGE
    uint8_t flags;         // retained, etc.
    uint8_t fragmentIndex;
    uint16_t fragmentCount;
    uint16_t topicLength;
    uint32_t payloadLength;
    uint32_t messageId;
};
```

`messageId` exists only for ESP-NOW reassembly/deduplication. It does not become part of the NMNW MQTT protocol.

Logical data is serialized as:

```text
topic bytes | payload bytes
```

and fragmented across ESP-NOW packets.

## Local-first routing

For ESP-NOW ingress the router deliberately does:

```text
1. local ESP-NOW fan-out except source
2. MQTT publish
```

so local devices do not depend on the broker round trip to communicate.

## Configuration

Edit:

```text
main/gateway_config.h
```

Set:

```cpp
WIFI_SSID
WIFI_PASSWORD
MQTT_BROKER_URI
MQTT_USERNAME        // optional
MQTT_PASSWORD        // optional
```

The default subscription is `#` for a transparent v1 bridge.

## Build

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

ESP-NOW is initialized only after Wi-Fi STA obtains an IP so it shares the AP-selected channel.

## Not in v1

- local MQTT broker
- web UI
- database / SD
- gateway-owned Resource model
- Arduino framework

## Next paired task

NightMare endpoints need an ESP-NOW transport adapter that can:

```text
send HELLO
encode topic + payload + retained
fragment messages
reassemble gateway messages
feed received topic/payload into the normal NightMare ingress path
```

That adapter should live beside MQTT as another transport, not inside `ResourcesManager`.
''',
}


def write_file(root: Path, relative: str, content: str, force: bool) -> str:
    path = root / relative
    if path.exists() and not force:
        return f"skip  {relative} (exists)"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content.lstrip(), encoding="utf-8")
    return f"write {relative}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    root = Path.cwd()
    print(f"Generating NMNW Gateway v1 in: {root}")
    for rel, content in FILES.items():
        print(write_file(root, rel, content, args.force))
    print("\nBefore building: edit main/gateway_config.h")
    print("Then: idf.py set-target esp32s3 && idf.py build")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
