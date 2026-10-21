#include "serial_console.h"
#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "espnow_transport.h"
#include "gateway_config.h"

namespace {
const char *TAG="console";

void resolve(const char *cmd) {
    if (!strcmp(cmd,"devices")) EspNowTransport::logDevices();
    else if (!strcmp(cmd,"reboot")) { ESP_LOGW(TAG,"rebooting..."); esp_restart(); }
    else ESP_LOGW(TAG,"unknown command: %s",cmd);
}

void task(void *) {
    char line[64];
    for (;;) {
        if (!fgets(line,sizeof(line),stdin)) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        size_t n=strlen(line);
        while (n && (line[n-1]=='\n'||line[n-1]=='\r')) line[--n]=0;
        if (n==0) continue;
        resolve(line);
    }
}
}

void SerialConsole::init() { configASSERT(xTaskCreate(task,"serial_console",CONSOLE_TASK_STACK,nullptr,CONSOLE_TASK_PRIORITY,nullptr)==pdPASS); }
