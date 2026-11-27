/*
 * UART bus scanner.
 *
 * A serial device can only be detected by listening: the scanner sets each
 * candidate GPIO as UART RX and listens at the most common baud rates. Captured
 * data is classified as GPS/NMEA, printable text, or unidentified binary data.
 *
 * Candidate pins are the M-Bus / Port-B pins that are free on the M5Stack Fire:
 *   GPIO13 (already used by the gps project), GPIO26/GPIO36 (Port B),
 *   GPIO18/GPIO19/GPIO23 (M-Bus, shared with the LCD SPI bus but free while the
 *   LCD is not driven).
 * The console UART0 (GPIO1/3) and Port C (GPIO16/17, wired to PSRAM) are not
 * scanned.
 */

#include "scan_uart.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "scan.uart";

#define SCAN_UART_PORT     UART_NUM_2
#define SCAN_UART_RX_BUFSZ 1024
#define SCAN_UART_CAPTURE  512

static const int scan_uart_rx_pins[] = {13, 26, 36, 18, 19, 23};
static const int scan_uart_bauds[]   = {4800, 9600, 19200, 38400, 115200};

typedef struct
{
    bool     nmea;
    bool     checksum_ok;
    bool     text;
    bool     binary;
    uint32_t baud;
    int      length;
    char     display[96];
} uart_finding_t;

static bool all_bytes_same(const uint8_t *data, int length)
{
    for (int i = 1; i < length; i++)
    {
        if (data[i] != data[0])
        {
            return false;
        }
    }
    return true;
}

static int printable_percent(const uint8_t *data, int length)
{
    int printable = 0;
    for (int i = 0; i < length; i++)
    {
        const uint8_t c = data[i];
        if (c == '\r' || c == '\n' || c == '\t' || (c >= 0x20 && c <= 0x7E))
        {
            printable++;
        }
    }
    return (length > 0) ? (printable * 100 / length) : 0;
}

static void sanitize_for_log(const uint8_t *data, int length, char *out, size_t out_size)
{
    size_t written = 0;
    for (int i = 0; i < length && written + 1 < out_size; i++)
    {
        const uint8_t c = data[i];
        if (c == '\r' || c == '\n' || c == '\t')
        {
            out[written++] = ' ';
        }
        else if (c >= 0x20 && c <= 0x7E)
        {
            out[written++] = (char)c;
        }
        else
        {
            out[written++] = '.';
        }
    }
    while (written > 0 && out[written - 1] == ' ')
    {
        written--;
    }
    out[written] = '\0';
}

static void format_hex(const uint8_t *data, int length, char *out, size_t out_size)
{
    size_t written = 0;
    for (int i = 0; i < length && written + 4 < out_size; i++)
    {
        written += (size_t)snprintf(out + written, out_size - written, "%02X ", (unsigned)data[i]);
    }
    if (written >= 2)
    {
        out[written - 1] = '\0';
    }
    else
    {
        out[0] = '\0';
    }
}

/* Look for a NMEA 0183 sentence ("$GNGGA,...*hh"). Also validates the checksum when present. */
static bool nmea_sample(const uint8_t *data, int length, char *sample, size_t sample_size, bool *checksum_ok)
{
    *checksum_ok = false;

    for (int start = 0; start + 6 < length; start++)
    {
        if (data[start] != '$')
        {
            continue;
        }

        bool valid = true;
        for (int i = 1; i <= 5; i++)
        {
            const uint8_t c = data[start + i];
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            {
                valid = false;
                break;
            }
        }
        if (!valid || data[start + 6] != ',')
        {
            continue;
        }

        size_t sample_length = 0;
        while ((size_t)start + sample_length < (size_t)length && sample_length + 1 < sample_size)
        {
            const uint8_t c = data[start + sample_length];
            if (c == '\r' || c == '\n')
            {
                break;
            }
            sample[sample_length] = (char)c;
            sample_length++;
        }
        sample[sample_length] = '\0';

        const char *star = strchr(sample, '*');
        if (star != NULL && strlen(star) >= 3)
        {
            uint8_t computed = 0;
            for (const char *p = sample + 1; p < star; p++)
            {
                computed ^= (uint8_t)*p;
            }

            unsigned int expected = 0;
            if (sscanf(star + 1, "%2x", &expected) == 1 && computed == expected)
            {
                *checksum_ok = true;
            }
        }
        return true;
    }
    return false;
}

static void classify_capture(const uint8_t *data, int length, uint32_t baud, uart_finding_t *finding)
{
    if (length <= 0 || all_bytes_same(data, length))
    {
        return; /* idle line, break condition or no device */
    }

    if (finding->nmea)
    {
        return; /* a confirmed GPS/NMEA stream is the strongest result; keep it */
    }

    char sample[96]  = {0};
    bool checksum_ok = false;
    if (nmea_sample(data, length, sample, sizeof(sample), &checksum_ok))
    {
        if (!finding->nmea)
        {
            finding->nmea        = true;
            finding->checksum_ok = checksum_ok;
            finding->baud        = baud;
            finding->length      = length;
            sanitize_for_log((const uint8_t *)sample, (int)strlen(sample), finding->display, sizeof(finding->display));
        }
        return;
    }

    if (length >= 4 && printable_percent(data, length) >= 90)
    {
        if (!finding->text || length > finding->length)
        {
            finding->text   = true;
            finding->baud   = baud;
            finding->length = length;
            sanitize_for_log(data, length, finding->display, sizeof(finding->display));
        }
        return;
    }

    if (!finding->text && !finding->nmea && length >= 16)
    {
        if (!finding->binary || length > finding->length)
        {
            finding->binary = true;
            finding->baud   = baud;
            finding->length = length;
            format_hex(data, (length < 16) ? length : 16, finding->display, sizeof(finding->display));
        }
    }
}

static void scan_pin(int rx_pin)
{
    ESP_LOGI(TAG, "  probing RX=%-2d ...", rx_pin);

    const uart_config_t uart_config = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(SCAN_UART_PORT, SCAN_UART_RX_BUFSZ, 0, 0, NULL, 0);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "  RX=%d: cannot install UART driver (%s)", rx_pin, esp_err_to_name(err));
        return;
    }

    err = uart_param_config(SCAN_UART_PORT, &uart_config);
    if (err == ESP_OK)
    {
        err = uart_set_pin(SCAN_UART_PORT, CONFIG_SCAN_UART_DUMMY_TX_GPIO, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "  RX=%d: cannot configure UART pins (%s)", rx_pin, esp_err_to_name(err));
        uart_driver_delete(SCAN_UART_PORT);
        return;
    }

    /* GPIO34-39 are input-only and have no internal pull-up. */
    if (rx_pin < 34)
    {
        (void)gpio_set_pull_mode((gpio_num_t)rx_pin, GPIO_PULLUP_ONLY);
    }

    uart_finding_t finding = {0};
    uint8_t        capture[SCAN_UART_CAPTURE];

    for (size_t i = 0; i < sizeof(scan_uart_bauds) / sizeof(scan_uart_bauds[0]); i++)
    {
        const uint32_t baud = (uint32_t)scan_uart_bauds[i];
        if (uart_set_baudrate(SCAN_UART_PORT, baud) != ESP_OK)
        {
            continue;
        }

        uart_flush_input(SCAN_UART_PORT);
        const int length = uart_read_bytes(SCAN_UART_PORT, capture, sizeof(capture), pdMS_TO_TICKS(CONFIG_SCAN_UART_PROBE_MS));
        if (length > 0)
        {
            classify_capture(capture, length, baud, &finding);
            if (finding.nmea)
            {
                break; /* baud rate identified, no need to probe the rest */
            }
        }
    }

    uart_driver_delete(SCAN_UART_PORT);

    if (finding.nmea)
    {
        ESP_LOGI(TAG, "  RX=%-2d : GPS/NMEA serial device @ %u baud%s", rx_pin, (unsigned)finding.baud, finding.checksum_ok ? " (valid checksum)" : "");
        ESP_LOGI(TAG, "           %s", finding.display);
    }
    else if (finding.text)
    {
        ESP_LOGI(TAG, "  RX=%-2d : serial text device @ %u baud (%d bytes)", rx_pin, (unsigned)finding.baud, finding.length);
        ESP_LOGI(TAG, "           \"%s\"", finding.display);
    }
    else if (finding.binary)
    {
        ESP_LOGI(TAG, "  RX=%-2d : unidentified data @ %u baud (%d bytes): %s", rx_pin, (unsigned)finding.baud, finding.length, finding.display);
    }
    else
    {
        ESP_LOGI(TAG, "  RX=%-2d : no data at %d baud rates", rx_pin, (int)(sizeof(scan_uart_bauds) / sizeof(scan_uart_bauds[0])));
    }
}

void scan_uart(void)
{
    ESP_LOGI(TAG, "--- UART scan: listening on candidate RX pins at 4800..115200 baud ---");
    ESP_LOGI(TAG, "  console UART0 (GPIO1/3) is used for logs and is not scanned");
    ESP_LOGI(TAG, "  PORT C (GPIO16/17) is wired to the PSRAM on the Fire v2.6 - not scanned");

    int    pins[sizeof(scan_uart_rx_pins) / sizeof(scan_uart_rx_pins[0]) + 1];
    size_t pin_count = sizeof(scan_uart_rx_pins) / sizeof(scan_uart_rx_pins[0]);
    memcpy(pins, scan_uart_rx_pins, sizeof(scan_uart_rx_pins));
#if CONFIG_SCAN_UART_CUSTOM_RX_GPIO >= 0
    pins[pin_count] = CONFIG_SCAN_UART_CUSTOM_RX_GPIO;
    pin_count++;
#endif

    for (size_t i = 0; i < pin_count; i++)
    {
        scan_pin(pins[i]);
    }

    ESP_LOGI(TAG, "--- UART scan done ---");
}
