idf_component_register(
    SRCS "main.c"
         "i2c_scan.c"
         "../../../hal/esp32/plexus_hal_nvs_config.c"
    INCLUDE_DIRS "."
                 "../../../hal/esp32"
    REQUIRES nvs_flash esp_wifi esp_event esp_timer driver esp_adc
    PRIV_REQUIRES c-sdk
)
