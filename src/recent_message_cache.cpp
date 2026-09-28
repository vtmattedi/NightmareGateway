#include "recent_message_cache.h"
#include "gateway_config.h"
bool RecentMessageCache::seen(const MacAddress &s, uint32_t id, uint32_t now) const {
    for (const auto &e : entries_) if (e.used && e.source == s && e.id == id && (now-e.at) <= NM_RECENT_MESSAGE_TTL_MS) return true;
    return false;
}
void RecentMessageCache::remember(const MacAddress &s, uint32_t id, uint32_t now) {
    entries_[next_] = {true, s, id, now};
    next_ = (next_ + 1) % NM_RECENT_MESSAGE_CACHE_SIZE;
}
