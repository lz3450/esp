/*
 * SPI bus scanner.
 *
 * SPI has no addresses, so devices are found by probing candidate chip-select
 * pins on a shared SPI bus. Three probe methods are used per CS pin:
 *   1. SD card:   CMD0 (GO_IDLE_STATE) in SPI mode, expecting an R1 response.
 *   2. SPI flash: JEDEC ID command 0x9F, expecting a known manufacturer ID.
 *   3. Generic:   read responses to 0x00/0xFF patterns, expecting data that is
 *                 neither floating-high nor floating-low.
 *
 * On the M5Stack Fire the default bus pins (18/23/19) are the LCD/TF card bus.
 * The LCD chip-select is held high so the LCD ignores the scan traffic.
 */

#include "scan_spi.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "scan.spi";

#define SCAN_SPI_HOST SPI2_HOST

/* TF card CS (4), M-Bus/Port-B free pins (13, 26, 5, 2). */
static const int spi_cs_candidates[] = {4, 13, 26, 5, 2};

typedef struct
{
    uint8_t     id;
    const char *name;
} jedec_manufacturer_t;

static const jedec_manufacturer_t jedec_manufacturers[] = {
    {0x01, "Spansion / Cypress"},
    {0x1F,     "Atmel / Adesto"},
    {0x20,        "Micron / ST"},
    {0x37,               "AMIC"},
    {0x9D,               "ISSI"},
    {0xBF,    "SST / Microchip"},
    {0xC2,           "Macronix"},
    {0xC8,         "GigaDevice"},
    {0xEF,            "Winbond"},
};

static const char *lookup_jedec_manufacturer(uint8_t id)
{
    for (size_t i = 0; i < sizeof(jedec_manufacturers) / sizeof(jedec_manufacturers[0]); i++)
    {
        if (jedec_manufacturers[i].id == id)
        {
            return jedec_manufacturers[i].name;
        }
    }
    return "unknown manufacturer";
}

static bool spi_transfer(spi_device_handle_t dev, const uint8_t *tx, uint8_t *rx, size_t length)
{
    spi_transaction_t transaction = {
        .length    = length * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_transmit(dev, &transaction) == ESP_OK;
}

static bool probe_sd_card(spi_device_handle_t dev, uint8_t *r1)
{
    /* 10 idle bytes, then CMD0 with its valid CRC, then clocks for the response. */
    uint8_t tx[24];
    uint8_t rx[24];

    memset(tx, 0xFF, sizeof(tx));
    memset(rx, 0x00, sizeof(rx));
    tx[10] = 0x40; /* CMD0 */
    tx[11] = 0x00;
    tx[12] = 0x00;
    tx[13] = 0x00;
    tx[14] = 0x00;
    tx[15] = 0x95; /* CRC7 for CMD0 */

    if (!spi_transfer(dev, tx, rx, sizeof(tx)))
    {
        return false;
    }

    for (size_t i = 16; i < sizeof(rx); i++)
    {
        if ((rx[i] & 0x80) == 0)
        {
            *r1 = rx[i];
            return true;
        }
    }
    return false;
}

static bool probe_jedec_id(spi_device_handle_t dev, uint8_t *id)
{
    const uint8_t tx[4] = {0x9F, 0x00, 0x00, 0x00};
    uint8_t       rx[4] = {0};

    if (!spi_transfer(dev, tx, rx, sizeof(tx)))
    {
        return false;
    }

    id[0] = rx[1];
    id[1] = rx[2];
    id[2] = rx[3];

    const bool all_zero = (id[0] == 0x00) && (id[1] == 0x00) && (id[2] == 0x00);
    const bool all_ff   = (id[0] == 0xFF) && (id[1] == 0xFF) && (id[2] == 0xFF);
    return !(all_zero || all_ff);
}

static bool probe_generic(spi_device_handle_t dev, uint8_t *response)
{
    const uint8_t tx_low[4]  = {0x00, 0x00, 0x00, 0x00};
    const uint8_t tx_high[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t       rx_low[4]  = {0};
    uint8_t       rx_high[4] = {0};

    if (!spi_transfer(dev, tx_low, rx_low, sizeof(tx_low)))
    {
        return false;
    }
    if (!spi_transfer(dev, tx_high, rx_high, sizeof(tx_high)))
    {
        return false;
    }

    memcpy(response, rx_low, sizeof(rx_low));
    memcpy(response + sizeof(rx_low), rx_high, sizeof(rx_high));

    bool all_zero = true;
    bool all_ff   = true;
    for (size_t i = 0; i < sizeof(rx_low) + sizeof(rx_high); i++)
    {
        if (response[i] != 0x00)
        {
            all_zero = false;
        }
        if (response[i] != 0xFF)
        {
            all_ff = false;
        }
    }
    return !(all_zero || all_ff);
}

void scan_spi(void)
{
    ESP_LOGI(TAG, "--- SPI scan: SCLK=%d MOSI=%d MISO=%d @ %d Hz ---", CONFIG_SCAN_SPI_SCLK_GPIO, CONFIG_SCAN_SPI_MOSI_GPIO, CONFIG_SCAN_SPI_MISO_GPIO, CONFIG_SCAN_SPI_FREQ_HZ);

#if CONFIG_SCAN_SPI_LCD_CS_GPIO >= 0
    /* The LCD shares the SPI bus; hold its chip-select high so it ignores the scan traffic. */
    const gpio_config_t lcd_cs_config = {
        .pin_bit_mask = 1ULL << CONFIG_SCAN_SPI_LCD_CS_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&lcd_cs_config));
    gpio_set_level((gpio_num_t)CONFIG_SCAN_SPI_LCD_CS_GPIO, 1);
#endif

    const spi_bus_config_t bus_config = {
        .mosi_io_num     = CONFIG_SCAN_SPI_MOSI_GPIO,
        .miso_io_num     = CONFIG_SCAN_SPI_MISO_GPIO,
        .sclk_io_num     = CONFIG_SCAN_SPI_SCLK_GPIO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 32,
    };

    esp_err_t err = spi_bus_initialize(SCAN_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to initialize SPI bus: %s", esp_err_to_name(err));
        return;
    }

    /* Pull MISO up so an empty bus reads 0xFF instead of floating noise. */
    (void)gpio_set_pull_mode((gpio_num_t)CONFIG_SCAN_SPI_MISO_GPIO, GPIO_PULLUP_ONLY);

    int    cs_list[sizeof(spi_cs_candidates) / sizeof(spi_cs_candidates[0]) + 1];
    size_t cs_count = sizeof(spi_cs_candidates) / sizeof(spi_cs_candidates[0]);
    memcpy(cs_list, spi_cs_candidates, sizeof(spi_cs_candidates));
#if CONFIG_SCAN_SPI_CUSTOM_CS_GPIO >= 0
    cs_list[cs_count] = CONFIG_SCAN_SPI_CUSTOM_CS_GPIO;
    cs_count++;
#endif

    int found = 0;
    for (size_t i = 0; i < cs_count; i++)
    {
        const int cs = cs_list[i];

        const spi_device_interface_config_t device_config = {
            .clock_speed_hz = CONFIG_SCAN_SPI_FREQ_HZ,
            .mode           = 0,
            .spics_io_num   = cs,
            .queue_size     = 1,
        };
        spi_device_handle_t dev = NULL;
        err                     = spi_bus_add_device(SCAN_SPI_HOST, &device_config, &dev);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "  CS=%d: cannot configure SPI device (%s)", cs, esp_err_to_name(err));
            continue;
        }

        uint8_t r1         = 0;
        uint8_t jedec[3]   = {0};
        uint8_t generic[8] = {0};

        if (probe_sd_card(dev, &r1))
        {
            found++;
            ESP_LOGI(TAG, "  CS=%-2d : TF/SD card responded (R1=0x%02X%s)", cs, (unsigned)r1, (r1 & 0x01) ? ", idle state" : "");
        }
        else if (probe_jedec_id(dev, jedec))
        {
            found++;
            ESP_LOGI(TAG, "  CS=%-2d : SPI flash, JEDEC ID %02X %02X %02X (%s)", cs,
                     (unsigned)jedec[0], (unsigned)jedec[1], (unsigned)jedec[2],
                     lookup_jedec_manufacturer(jedec[0]));
        }
        else if (probe_generic(dev, generic))
        {
            found++;
            ESP_LOGI(TAG, "  CS=%-2d : SPI device responded: %02X %02X %02X %02X | %02X %02X %02X %02X (verify manually)", cs,
                     (unsigned)generic[0], (unsigned)generic[1], (unsigned)generic[2], (unsigned)generic[3],
                     (unsigned)generic[4], (unsigned)generic[5], (unsigned)generic[6], (unsigned)generic[7]);
        }

        spi_bus_remove_device(dev);
    }

    spi_bus_free(SCAN_SPI_HOST);

    if (found == 0)
    {
        ESP_LOGI(TAG, "  no SPI device responded on the CS candidates");
    }
    ESP_LOGI(TAG, "--- SPI scan done: %d device(s) ---", found);
}
