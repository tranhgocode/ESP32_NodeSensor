#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "indicator/status_led.h"

static const char *TAG = "status_led";
static bool initialized;

static void cleanup_gpio(gpio_num_t pin)
{
    const esp_err_t result = gpio_reset_pin(pin);
    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "LED GPIO cleanup failed: %d", (int)result);
    }
}

node_result_t status_led_init(void)
{
    const gpio_num_t pin = (gpio_num_t)BOARD_STATUS_LED_GPIO;
    initialized = false;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin) || gpio_reset_pin(pin) != ESP_OK)
    {
        return NODE_INIT_FAILED;
    }
    /* Ghi trước mức LOW rồi mới bật ngõ ra, tránh LED lóe sáng khi khởi tạo. */
    if (gpio_set_level(pin, 0) != ESP_OK)
    {
        cleanup_gpio(pin);
        return NODE_INIT_FAILED;
    }
    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << BOARD_STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&config) != ESP_OK)
    {
        cleanup_gpio(pin);
        return NODE_INIT_FAILED;
    }
    initialized = true;
    return NODE_OK;
}

node_result_t status_led_set(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!initialized)
    {
        return NODE_INIT_FAILED;
    }
    const uint32_t level = (red != 0U || green != 0U || blue != 0U) ? 1U : 0U;
    return gpio_set_level((gpio_num_t)BOARD_STATUS_LED_GPIO, level) == ESP_OK ?
        NODE_OK : NODE_IO_ERROR;
}

node_result_t status_led_off(void)
{
    return status_led_set(0, 0, 0);
}
