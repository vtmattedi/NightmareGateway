#pragma once
#include "esp_err.h"
#include "gateway_types.h"
namespace EspNowTransport { esp_err_t init(); bool sendToAllExcept(const GatewayMessage &, const MacAddress *); }
