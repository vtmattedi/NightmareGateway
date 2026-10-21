#pragma once

bool chipTempInit();
bool chipTempRead(float &temperatureC);
void chipTempDeinit();

// Reads the chip temperature into the "chip:temperature" Resource at its
// hardware poll interval. Call from the gateway tick, which owns Resources
// (NightMareGatewayConfig::onTick).
void chipTempTick();
