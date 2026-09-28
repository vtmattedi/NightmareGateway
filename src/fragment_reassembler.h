#pragma once
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
