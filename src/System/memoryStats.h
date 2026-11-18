#pragma once

// RAM and PSRAM free, as a percentage of what the chip has, exposed to the
// network as the "memory:internal_free" and "memory:psram_free" Resources
// (percent, like "chip:temperature" is degrees). Internal RAM is the scarce
// one: Wi-Fi, lwIP and TLS draw from it, PSRAM only holds what is placed there.

struct MemoryUsage
{
    unsigned internalFree = 0, internalTotal = 0;
    unsigned psramFree = 0, psramTotal = 0;
    // 0..100, or -1 when the chip has none of that memory.
    float internalFreePct() const { return internalTotal ? 100.0f * internalFree / internalTotal : -1.0f; }
    float psramFreePct() const { return psramTotal ? 100.0f * psramFree / psramTotal : -1.0f; }
};

MemoryUsage memoryUsage();

// Declares the Resources' hardware policy. Before the gateway starts: a policy
// set afterwards would republish the manifest outside the gateway tick.
void memoryInit();

// Updates the Resources at their poll interval. Call from the gateway tick,
// which owns Resources (NightMareGatewayConfig::onTick).
void memoryTick();
