idf_component_register(
    SRCS
    "scan_main.c"
    "scan_i2c.c"
    "scan_spi.c"
    "scan_uart.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
    esp_driver_i2c
    esp_driver_spi
    esp_driver_uart
    esp_driver_gpio
    esp_timer
)
