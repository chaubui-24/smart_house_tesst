#include <string.h>
#include <stdlib.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_standard_types.h>

#include <app_network.h>
#include <app_insights.h>

#include "app_priv.h"

static const char *TAG = "app_main";

esp_rmaker_device_t *temp_sensor_device;

void app_main(void)
{
    /* Initialize sensor */
    ESP_ERROR_CHECK(app_driver_init());

    /* Initialize NVS */
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {

        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);

    /* Initialize Wi-Fi */
    app_network_init();

    /* Initialize RainMaker */
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = false,
    };

    esp_rmaker_node_t *node = esp_rmaker_node_init(
        &rainmaker_cfg,
        "ESP RainMaker Device",
        "Greenhouse"
    );

    if (node == NULL) {
        ESP_LOGE(TAG, "Could not initialize RainMaker node");
        abort();
    }

    /* Create temperature device */
    temp_sensor_device = esp_rmaker_temp_sensor_device_create(
        "Temperature Sensor",
        NULL,
        app_get_current_temperature()
    );

    if (temp_sensor_device == NULL) {
        ESP_LOGE(TAG, "Could not create temperature device");
        abort();
    }

    /* Add humidity parameter */
    esp_rmaker_param_t *humidity_param =
        esp_rmaker_param_create(
            "Humidity",
            "esp.param.humidity",
            esp_rmaker_float(app_get_current_humidity()),
            PROP_FLAG_READ
        );

    if (humidity_param == NULL) {
        ESP_LOGE(TAG, "Could not create humidity parameter");
        abort();
    }

    ESP_ERROR_CHECK(
        esp_rmaker_device_add_param(
            temp_sensor_device,
            humidity_param
        )
    );

    esp_rmaker_node_add_device(node, temp_sensor_device);

    /* Enable OTA */
    esp_rmaker_ota_enable_default();

    /* Enable Insights */
    app_insights_enable();

    /* Start RainMaker */
    esp_rmaker_start();

    /* Start Wi-Fi provisioning/connection */
    err = app_network_start(POP_TYPE_RANDOM);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start Wi-Fi");
        abort();
    }

    /* Start sensor reporting */
    ESP_ERROR_CHECK(app_sensor_start());

    ESP_LOGI(TAG, "Greenhouse RainMaker started successfully");
}