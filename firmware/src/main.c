#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#define RGB_LED_GPIO 48
#define BLINK_INTERVAL_MS 500
#define LED_BRIGHTNESS 16 // Gia tri 0-255 cho moi kenh mau.

static const char *TAG = "rgb_led";

void app_main(void)
{
    led_strip_handle_t led;
    const led_strip_config_t led_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_SK6812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    ESP_LOGI(TAG, "Starting SK6812 RGB test on GPIO%d", RGB_LED_GPIO);
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&led_config, &rmt_config, &led));
    ESP_LOGI(TAG, "RMT driver initialized; clearing LED");
    ESP_ERROR_CHECK(led_strip_clear(led));
    ESP_LOGI(TAG, "LED clear completed");

    const uint8_t colors[][3] = {
        {LED_BRIGHTNESS, 0, 0}, // Do
        {0, LED_BRIGHTNESS, 0}, // Xanh la
        {0, 0, LED_BRIGHTNESS}, // Xanh duong
    };

    while (1)
    {
        for (unsigned i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i)
        {
            ESP_LOGI(TAG, "Sending RGB(%u, %u, %u)",
                     (unsigned)colors[i][0], (unsigned)colors[i][1], (unsigned)colors[i][2]);
            ESP_ERROR_CHECK(led_strip_set_pixel(led, 0,
                                                colors[i][0], colors[i][1], colors[i][2]));
            ESP_ERROR_CHECK(led_strip_refresh(led));
            ESP_LOGI(TAG, "Color sent on GPIO%d", RGB_LED_GPIO);
            vTaskDelay(pdMS_TO_TICKS(BLINK_INTERVAL_MS));

            ESP_ERROR_CHECK(led_strip_clear(led));
            vTaskDelay(pdMS_TO_TICKS(BLINK_INTERVAL_MS));
        }
    }
}
