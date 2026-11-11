#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_config.h"
#include "dht.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sensor/dht11.h"

/* Dải DHT11 kiểu cũ, tương ứng decoder DHT_TYPE_DHT11 của library 1.2.0.
 * Không mở rộng sang DHT11 V1.3 chỉ bằng đổi hằng: encoding dấu khác nhau.
 * Nguồn và giới hạn tương thích được ghi trong logs/wiring.md. */
#define DHT11_TEMPERATURE_MIN_X10 0
#define DHT11_TEMPERATURE_MAX_X10 500
#define DHT11_HUMIDITY_MIN_X10 200
#define DHT11_HUMIDITY_MAX_X10 900

static const char *TAG = "dht11";
static bool initialized;

node_result_t dht11_init(void)
{
    const gpio_num_t pin = (gpio_num_t)BOARD_DHT11_GPIO;
    initialized = false;

    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin))
    {
        return NODE_INIT_FAILED;
    }

    /* Reset tắt output trước. Đặt latch = 1 khi output còn tắt để lúc bật
     * open-drain không vô tình kéo DATA thấp như một start signal sớm. */
    if (gpio_reset_pin(pin) != ESP_OK)
    {
        return NODE_INIT_FAILED;
    }
    if (gpio_set_level(pin, 1) != ESP_OK)
    {
        return NODE_INIT_FAILED;
    }

    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << BOARD_DHT11_GPIO,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&config) != ESP_OK)
    {
        /* Chưa ready nếu cấu hình dở dang. Nhả output và ghi riêng lỗi cleanup
         * nếu driver cũng không reset được, không che lỗi bằng NODE_OK. */
        const esp_err_t cleanup_result = gpio_reset_pin(pin);
        if (cleanup_result != ESP_OK)
        {
            ESP_LOGE(TAG, "GPIO cleanup failed: %d", (int)cleanup_result);
        }
        return NODE_INIT_FAILED;
    }

    initialized = true;
    return NODE_OK;
}

node_result_t dht11_read(node_measurement_t *out)
{
    if (out == NULL)
    {
        return NODE_INVALID_ARG;
    }
    if (!initialized)
    {
        return NODE_INIT_FAILED;
    }

    /* Library nhận humidity trước temperature và đã trả giá trị x10.
     * Dùng biến signed cục bộ: không ghi output caller trước khi biết kết quả
     * và không cast humidity âm thành số unsigned rất lớn. Chỉ đọc một lần. */
    int16_t humidity_x10 = 0;
    int16_t temperature_x10 = 0;
    const esp_err_t result = dht_read_data(DHT_TYPE_DHT11,
                                          (gpio_num_t)BOARD_DHT11_GPIO,
                                          &humidity_x10, &temperature_x10);
    switch (result)
    {
        case ESP_OK:
            break;
        case ESP_ERR_TIMEOUT:
            return NODE_TIMEOUT;
        case ESP_ERR_INVALID_CRC:
            return NODE_CHECKSUM_ERROR;
        default:
            return NODE_IO_ERROR;
    }

    if (temperature_x10 < DHT11_TEMPERATURE_MIN_X10 ||
        temperature_x10 > DHT11_TEMPERATURE_MAX_X10 ||
        humidity_x10 < DHT11_HUMIDITY_MIN_X10 ||
        humidity_x10 > DHT11_HUMIDITY_MAX_X10)
    {
        return NODE_OUT_OF_RANGE;
    }

    *out = (node_measurement_t){
        .temperature_x10 = temperature_x10,
        .humidity_x10 = (uint16_t)humidity_x10,
    };
    return NODE_OK;
}
