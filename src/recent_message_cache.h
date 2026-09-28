#pragma once
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
