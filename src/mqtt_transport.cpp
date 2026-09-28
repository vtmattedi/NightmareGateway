#include "mqtt_transport.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "gateway_config.h"
#include "gateway_router.h"

namespace {
const char *TAG="mqtt"; esp_mqtt_client_handle_t client=nullptr;
void subscribe_no_local() {
    esp_mqtt5_subscribe_property_config_t p={}; p.no_local_flag=true; p.retain_as_published_flag=true; p.retain_handle=0;
    ESP_ERROR_CHECK(esp_mqtt5_client_set_subscribe_property(client,&p));
    int id=esp_mqtt_client_subscribe(client,MQTT_SUBSCRIPTION,0);
    ESP_LOGI(TAG,"subscribe %s no-local=true msg=%d",MQTT_SUBSCRIPTION,id);
}
void handler(void *,esp_event_base_t,int32_t event_id,void *event_data) {
    auto *e=(esp_mqtt_event_handle_t)event_data;
    switch((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: subscribe_no_local(); break;
        case MQTT_EVENT_DATA: {
            GatewayMessage m; m.ingress=GatewayIngress::Mqtt; m.topic.assign(e->topic,e->topic_len); m.retained=e->retain!=0;
            if(e->data && e->data_len>0) m.payload.assign((uint8_t*)e->data,(uint8_t*)e->data+e->data_len);
            GatewayRouter::enqueue(m); break;
        }
        case MQTT_EVENT_ERROR: ESP_LOGE(TAG,"MQTT error"); break;
        default: break;
    }
}
}

esp_err_t MqttTransport::init() {
    esp_mqtt_client_config_t cfg={};
    cfg.broker.address.uri=MQTT_BROKER_URI;
    cfg.credentials.username=MQTT_USERNAME[0]?MQTT_USERNAME:nullptr;
    cfg.credentials.authentication.password=MQTT_PASSWORD[0]?MQTT_PASSWORD:nullptr;
    cfg.credentials.client_id=GATEWAY_NAME;
    cfg.session.protocol_ver=MQTT_PROTOCOL_V_5;
    client=esp_mqtt_client_init(&cfg); if(!client) return ESP_FAIL;
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client,ESP_EVENT_ANY_ID,handler,nullptr));
    return esp_mqtt_client_start(client);
}

bool MqttTransport::publish(const GatewayMessage &m) {
    if(!client) return false;
    int id=esp_mqtt_client_publish(client,m.topic.c_str(),(const char*)m.payload.data(),(int)m.payload.size(),0,m.retained?1:0);
    return id>=0;
}
