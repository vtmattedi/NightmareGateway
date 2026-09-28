#pragma once
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
