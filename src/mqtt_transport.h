#pragma once
#include "esp_err.h"
#include "gateway_types.h"
namespace MqttTransport { esp_err_t init(); bool publish(const GatewayMessage &); }
