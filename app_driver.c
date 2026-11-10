#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "driver/i2c_master.h"

#include "esp_rmaker_core.h"
#include "esp_rmaker_standard_types.h"

#include "app_reset.h"
#include "app_priv.h"

#define BUTTON_GPIO          CONFIG_EXAMPLE_BOARD_BUTTON_GPIO
#define BUTTON_ACTIVE_LEVEL  0

#define WIFI_RESET_BUTTON_TIMEOUT       3
#define FACTORY_RESET_BUTTON_TIMEOUT    10

static const char *TAG = "AHT10";

static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t aht10_dev;



static float g_temperature = 25.0f;
static float g_humidity = 50.0f;

/* Khởi tạo I2C */
static esp_err_t aht10_i2c_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = AHT10_SDA_GPIO,
        .scl_io_num = AHT10_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AHT10_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    ESP_ERROR_CHECK(
        i2c_master_bus_add_device(
            i2c_bus,
            &dev_config,
            &aht10_dev
        )
    );

    return ESP_OK;
}

/* Khởi tạo cảm biến AHT10 */
static esp_err_t aht10_init(void)
{
    uint8_t init_cmd[3] = {
        0xE1, 0x08, 0x00
    };

    esp_err_t ret = i2c_master_transmit(
        aht10_dev,
        init_cmd,
        sizeof(init_cmd),
        1000
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AHT10 initialization failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "AHT10 initialized");

    return ESP_OK;
}

/* Đọc dữ liệu AHT10 */
static esp_err_t aht10_read(float *temperature, float *humidity)
{
    uint8_t measure_cmd[3] = {0xAC, 0x33, 0x00};

    ESP_LOGI(TAG, "AHT10: Sending measure command");

    esp_err_t ret = i2c_master_transmit(
        aht10_dev,
        measure_cmd,
        sizeof(measure_cmd),
        1000
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MEASURE TRANSMIT ERROR: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(80));

    uint8_t data[6] = {0};

    for (int i = 0; i < 10; i++) {

        ret = i2c_master_receive(
            aht10_dev,
            data,
            sizeof(data),
            1000
        );

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "DATA RECEIVE ERROR: %s",
                     esp_err_to_name(ret));
            return ret;
        }

        if (!(data[0] & 0x80)) {
            break;
        }

        if (i == 9) {
            ESP_LOGE(TAG, "AHT10 measurement timeout");
            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    uint32_t raw_humidity =
        ((uint32_t)data[1] << 12) |
        ((uint32_t)data[2] << 4) |
        ((uint32_t)data[3] >> 4);

    uint32_t raw_temperature =
        (((uint32_t)data[3] & 0x0F) << 16) |
        ((uint32_t)data[4] << 8) |
        data[5];

    *humidity = (float)raw_humidity * 100.0f / 1048576.0f;

    *temperature =
        (float)raw_temperature * 200.0f / 1048576.0f - 50.0f;

    return ESP_OK;
}

/* Cập nhật dữ liệu và gửi lên RainMaker */
static void app_sensor_task(void *pvParameters)
{
    while (1) {
        float temperature;
        float humidity;

        esp_err_t ret = aht10_read(&temperature, &humidity);

        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read AHT10: %s",
                     esp_err_to_name(ret));
        } else {
            g_temperature = temperature;
            g_humidity = humidity;

            ESP_LOGI(TAG,
                     "Temperature: %.2f C | Humidity: %.2f %%",
                     g_temperature,
                     g_humidity);

            if (temp_sensor_device != NULL) {
                esp_rmaker_param_t *temp_param =
                    esp_rmaker_device_get_param_by_type(
                        temp_sensor_device,
                        ESP_RMAKER_PARAM_TEMPERATURE
                    );

                esp_rmaker_param_t *humidity_param =
                    esp_rmaker_device_get_param_by_name(
                        temp_sensor_device,
                        "Humidity"
                    );

                if (temp_param != NULL) {
                    esp_rmaker_param_update_and_report(
                        temp_param,
                        esp_rmaker_float(g_temperature)
                    );
                }

                if (humidity_param != NULL) {
                    esp_rmaker_param_update_and_report(
                        humidity_param,
                        esp_rmaker_float(g_humidity)
                    );
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(REPORTING_PERIOD * 1000));
    }
}

float app_get_current_temperature(void)
{
    return g_temperature;
}

float app_get_current_humidity(void)
{
    return g_humidity;
}

/* Khởi tạo driver */
esp_err_t app_driver_init(void)
{
    ESP_LOGI(TAG, "Initializing AHT10...");

    ESP_ERROR_CHECK(aht10_i2c_init());
    ESP_ERROR_CHECK(aht10_init());

    /* Đọc dữ liệu lần đầu */
    ESP_ERROR_CHECK(
        aht10_read(&g_temperature, &g_humidity)
    );

    ESP_LOGI(TAG,
             "Initial: %.2f C, %.2f %%",
             g_temperature,
             g_humidity);

    app_reset_button_register(
        app_reset_button_create(BUTTON_GPIO, BUTTON_ACTIVE_LEVEL),
        WIFI_RESET_BUTTON_TIMEOUT,
        FACTORY_RESET_BUTTON_TIMEOUT
    );

    return ESP_OK;
}

/* Chỉ bắt đầu gửi dữ liệu sau khi Wi-Fi đã kết nối */
esp_err_t app_sensor_start(void)
{
    BaseType_t ret = xTaskCreate(
        app_sensor_task,
        "aht10_task",
        4096,
        NULL,
        5,
        NULL
    );

    if (ret != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "AHT10 reporting task started");
    return ESP_OK;
}