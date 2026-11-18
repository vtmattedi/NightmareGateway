#include "memoryStats.h"

#include "esp_heap_caps.h"
#include <Core/NetResources.h>
#include <Core/PlatformClock.h>

namespace
{
    constexpr int32_t DefaultPollMs = 10000;

    ManagedSensor<float> internalFree("memory:internal_free");
    ManagedSensor<float> psramFree("memory:psram_free");

    void declare(ManagedSensor<float> &resource, const char *note)
    {
        resource.setHardwarePolicy({
            .pollMs = DefaultPollMs,
            .flags = REPORT_HW_CONNECTION | CONFIGURABLE_POLL | CAN_DISABLE,
            .note = note,
        });
    }

    void publish(ManagedSensor<float> &resource, float pct)
    {
        const bool present = pct >= 0.0f; // no PSRAM on the board: not connected, not 0%
        resource.setHardwareConnected(present);
        if (present)
            resource.setValue(pct);
    }
}

MemoryUsage memoryUsage()
{
    MemoryUsage usage;
    usage.internalFree = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    usage.internalTotal = (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    usage.psramFree = (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    usage.psramTotal = (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    return usage;
}

void memoryInit()
{
    declare(internalFree, "internal RAM free, % of the heap");
    declare(psramFree, "PSRAM free, % of the heap");
}

void memoryTick()
{
    static bool polled = false;
    static uint32_t lastPollMs = 0;

    if (!internalFree.hardwareEnabled())
        return;
    const int32_t pollMs = internalFree.hardwarePollMs() > 0 ? internalFree.hardwarePollMs() : DefaultPollMs;
    const uint32_t now = NightMare::Platform::monotonicMillis();
    if (polled && now - lastPollMs < static_cast<uint32_t>(pollMs))
        return;
    polled = true;
    lastPollMs = now;

    const MemoryUsage usage = memoryUsage();
    publish(internalFree, usage.internalFreePct());
    publish(psramFree, usage.psramFreePct());
}
