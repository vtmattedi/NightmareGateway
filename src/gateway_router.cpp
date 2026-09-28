#include "gateway_router.h"
#include <memory>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "espnow_transport.h"
#include "gateway_config.h"
#include "mqtt_transport.h"

namespace {
QueueHandle_t q=nullptr;
void task(void *) {
    GatewayMessage *m=nullptr;
    for (;;) {
        if (xQueueReceive(q,&m,portMAX_DELAY)!=pdTRUE) continue;
        std::unique_ptr<GatewayMessage> owned(m);
        if (!m) continue;
        if (m->ingress==GatewayIngress::EspNow) {
            EspNowTransport::sendToAllExcept(*m,&m->sourceMac); // local first
            MqttTransport::publish(*m);
        } else {
            EspNowTransport::sendToAllExcept(*m,nullptr);
        }
    }
}
}
void GatewayRouter::init() { q=xQueueCreate(ROUTER_QUEUE_DEPTH,sizeof(GatewayMessage*)); configASSERT(q); configASSERT(xTaskCreate(task,"gateway_router",ROUTER_TASK_STACK,nullptr,ROUTER_TASK_PRIORITY,nullptr)==pdPASS); }
bool GatewayRouter::enqueue(const GatewayMessage &m) { auto *c=new(std::nothrow) GatewayMessage(m); if(!c) return false; if(xQueueSend(q,&c,0)!=pdTRUE){delete c; return false;} return true; }
