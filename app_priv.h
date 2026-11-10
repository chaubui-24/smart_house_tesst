#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_rmaker_core.h"

#define REPORTING_PERIOD 10

#define AHT10_SDA_GPIO 8
#define AHT10_SCL_GPIO 9
#define AHT10_I2C_ADDR 0x38

extern esp_rmaker_device_t *temp_sensor_device;

esp_err_t app_driver_init(void);
esp_err_t app_sensor_start(void);

float app_get_current_temperature(void);
float app_get_current_humidity(void);