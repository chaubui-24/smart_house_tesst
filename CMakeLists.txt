idf_component_register(
    SRCS "app_driver.c" "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_i2c
        nvs_flash
)