#pragma once
#include <stdio.h>
#include <time.h>
#include "esp_err.h"

// Starts SNTP and blocks (up to timeout_ms) until it has synced the system
// clock. With 0 it only starts it: the clock then syncs in the background once
// the station has an IP, and nothing waits for it.
// Timezone is fixed to GMT-3 (Brasilia time, no DST).
esp_err_t ntp_sync_start(uint32_t timeout_ms = 10000);
