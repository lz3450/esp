/*
 * scan - bus scanner for the M5Stack Fire v2.6.
 *
 * Probes the I2C, SPI and UART buses and reports devices that respond.
 * The scan runs once at boot; reset the board to run it again.
 *
 * Target board: ESP32-D0WDQ6-V3, 16 MB flash, 8 MB PSRAM.
 * PSRAM is left disabled on purpose (see README.md).
 */

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "scan_i2c.h"
#include "scan_spi.h"
#include "scan_uart.h"

static const char *TAG = "scan";

static void log_board_inventory(void)
{
    ESP_LOGI(TAG, "--- onboard inventory (M5Stack Fire v2.6) ---");
    ESP_LOGI(TAG, "LCD  : ILI9342C 320x240 on SPI2: SCLK=18 MOSI=23 MISO=19 CS=14 DC=27 RST=33 BL=32");
    ESP_LOGI(TAG, "I2C  : MPU6886 IMU @0x68, IP5306 PMIC @0x75 (SDA=21 SCL=22)");
    ESP_LOGI(TAG, "       (older FIRE revisions also carry a BMM150 magnetometer @0x10; the v2.6 has none)");
    ESP_LOGI(TAG, "Other: TF card CS=4 (LCD SPI bus), LED bar GPIO15, speaker GPIO25, mic GPIO34, keys 37/38/39");
    ESP_LOGI(TAG, "Note : PORT C (GPIO16/17) is wired to the PSRAM; PSRAM is disabled here, but those pins");
    ESP_LOGI(TAG, "       are still not scanned so the PSRAM chip is never disturbed.");
}

void app_main(void)
{
    const int64_t start_us = esp_timer_get_time();

    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, " ESP32 device scan (M5Stack Fire v2.6)");
    ESP_LOGI(TAG, "==============================================");
    log_board_inventory();

#if CONFIG_SCAN_I2C_ENABLE
    scan_i2c();
#endif

#if CONFIG_SCAN_SPI_ENABLE
    scan_spi();
#endif

#if CONFIG_SCAN_UART_ENABLE
    scan_uart();
#endif

    const int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, " scan complete in %" PRId64 " ms", elapsed_ms);
    ESP_LOGI(TAG, " reset the board to run the scan again");
    ESP_LOGI(TAG, "==============================================");

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
