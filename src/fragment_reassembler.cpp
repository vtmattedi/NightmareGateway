#include "fragment_reassembler.h"
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
