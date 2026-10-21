#include "chipTemperature.h"

#include "driver/temperature_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <Core/NetResources.h>
#include <Core/PlatformClock.h>

namespace
{
    constexpr const char *TAG = "chip_temp";
    temperature_sensor_handle_t sensor = nullptr;
    SemaphoreHandle_t mutex = nullptr;

    constexpr int32_t DefaultPollMs = 10000;

    // The ESP32-S3 internal sensor, exposed to the network. Bound by
    // startNightMareESP() like every Resource declared before it.
    ManagedSensor<float> temperature("chip:temperature");
}

bool chipTempInit()
{
    if (sensor != nullptr)
        return true;

    // Before the gateway binds it: a policy set afterwards would republish
    // the manifest outside the gateway tick.
    temperature.setHardwarePolicy({
        .pollMs = DefaultPollMs,
        .flags = REPORT_HW_CONNECTION | CONFIGURABLE_POLL | CAN_DISABLE,
        .note = "ESP32-S3 internal sensor",
    });

    if (mutex == nullptr)
    {
        mutex = xSemaphoreCreateMutex();
        if (mutex == nullptr)
        {
            ESP_LOGE(TAG, "Failed to create temperature sensor mutex");
            return false;
        }
    }

    temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
    esp_err_t err = temperature_sensor_install(&config, &sensor);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "temperature_sensor_install failed: %s", esp_err_to_name(err));
        sensor = nullptr;
        return false;
    }

    err = temperature_sensor_enable(sensor);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "temperature_sensor_enable failed: %s", esp_err_to_name(err));
        temperature_sensor_uninstall(sensor);
        sensor = nullptr;
        return false;
    }

    return true;
}

bool chipTempRead(float &temperatureC)
{
    if (sensor == nullptr || mutex == nullptr)
        return false;

    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return false;

    const esp_err_t err = temperature_sensor_get_celsius(sensor, &temperatureC);
    xSemaphoreGive(mutex);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "temperature read failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

void chipTempDeinit()
{
    if (sensor == nullptr || mutex == nullptr)
        return;

    xSemaphoreTake(mutex, portMAX_DELAY);
    temperature_sensor_disable(sensor);
    temperature_sensor_uninstall(sensor);
    sensor = nullptr;
    xSemaphoreGive(mutex);
}

void chipTempTick()
{
    static bool polled = false;
    static uint32_t lastPollMs = 0;

    if (!temperature.hardwareEnabled())
        return;
    // 0 is "firmware default"; a negative poll is a disabled one, caught above.
    const int32_t pollMs = temperature.hardwarePollMs() > 0 ? temperature.hardwarePollMs() : DefaultPollMs;
    const uint32_t now = NightMare::Platform::monotonicMillis();
    if (polled && now - lastPollMs < static_cast<uint32_t>(pollMs))
        return;
    polled = true;
    lastPollMs = now;

    float temperatureC;
    const bool ok = chipTempRead(temperatureC);
    temperature.setHardwareConnected(ok);
    if (ok)
        temperature.setValue(temperatureC);
}
