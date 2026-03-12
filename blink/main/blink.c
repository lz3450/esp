#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"

#define LED_NUM 10

static const char *TAG = "blink";

static led_strip_handle_t led_strip;

static void configure_led(led_strip_handle_t *strip)
{
    /* LED strip initialization with the GPIO and pixels number*/
    led_strip_config_t strip_config = {
        .strip_gpio_num         = CONFIG_BLINK_GPIO,                 // The GPIO that connected to the LED strip's data line
        .max_leds               = LED_NUM,                           // The number of LEDs in the strip,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, // Pixel format of your LED strip
        .led_model              = LED_MODEL_SK6812,                  // LED strip model
        .flags.invert_out       = false,                             // whether to invert the output signal (useful when your hardware has a level inverter)
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src        = RMT_CLK_SRC_DEFAULT, // different clock source can lead to different power consumption
        .resolution_hz  = 10 * 1000 * 1000,    // 10MHz
        .flags.with_dma = false,               // whether to enable the DMA feature
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, strip));
    /* Set all LED off to clear all pixels */
    led_strip_clear(*strip);
}

void app_main(void)
{
    configure_led(&led_strip);

    for (;;)
    {
        for (uint32_t i = 0; i < LED_NUM; i++)
        {
            ESP_LOGI(TAG, "Red");
            led_strip_set_pixel(led_strip, i, 16, 0, 0);
            vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS);
            led_strip_refresh(led_strip);
        }
        led_strip_clear(led_strip);

        for (uint32_t i = 0; i < LED_NUM; i++)
        {
            ESP_LOGI(TAG, "Green");
            led_strip_set_pixel(led_strip, i, 0, 16, 0);
            vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS);
            led_strip_refresh(led_strip);
        }
        led_strip_clear(led_strip);

        for (uint32_t i = 0; i < LED_NUM; i++)
        {
            ESP_LOGI(TAG, "Blue");
            led_strip_set_pixel(led_strip, i, 0, 0, 16);
            vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS);
            led_strip_refresh(led_strip);
        }
        led_strip_clear(led_strip);
    }
}
