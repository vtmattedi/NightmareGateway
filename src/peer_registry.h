#pragma once
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
