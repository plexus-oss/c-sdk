idf_component_register(
    SRCS "main.c"
         "../../../hal/esp32/plexus_hal_nvs_config.c"
    INCLUDE_DIRS "."
                 "../../../hal/esp32"
    REQUIRES nvs_flash esp_wifi esp_event esp_timer driver
)
