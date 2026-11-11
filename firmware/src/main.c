/*
 * Bài thử Task 4 chỉ init DHT11, chờ 5 giây rồi đọc một lần mỗi lượt.
 * Đây là vTaskDelay cho bench test, chưa phải Light-sleep hoặc chu kỳ
 * DATA/ERROR/ACK. Task 8 sẽ thay phần thử tạm bằng ứng dụng node đầy đủ.
 */
#include "esp_log.h"
#include "node_config.h"
#include "node_types.h"
#include "board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor/dht11.h"

static const char *TAG = "node";

/**
 * Điểm vào do ESP-IDF gọi khi ứng dụng bắt đầu.
 *
 * Nếu GPIO init lỗi, log và dừng bài thử; không đọc trên driver chưa ready.
 * Chờ trước cả lượt đầu để DHT11 ổn định sau cấp nguồn. Mỗi lượt tạo sample
 * riêng, chỉ in giá trị khi NODE_OK; không in lại mẫu cũ sau timeout/checksum.
 */
void app_main(void)
{
    const node_config_t config = NODE_CONFIG_DEFAULT;

    ESP_LOGI(TAG, "DHT11 bench test (Task 4), GPIO=%d", BOARD_DHT11_GPIO);
    ESP_LOGI(TAG, "Node=0x%02X gateway=0x%02X", (unsigned)config.node_addr,
             (unsigned)config.gateway_addr);
    ESP_LOGI(TAG, "Configured sleep=%u ms TX timeout=%u ms ACK timeout=%u ms",
             (unsigned)config.sleep_ms, (unsigned)config.tx_timeout_ms,
             (unsigned)config.ack_timeout_ms);

    const node_result_t init_result = dht11_init();
    if (init_result != NODE_OK)
    {
        ESP_LOGE(TAG, "DHT11 GPIO init failed: node_result=%d", (int)init_result);
        return;
    }

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(config.sleep_ms));

        node_measurement_t sample = {0};
        const node_result_t result = dht11_read(&sample);
        if (result != NODE_OK)
        {
            ESP_LOGW(TAG, "DHT11 read failed: node_result=%d; sample discarded",
                     (int)result);
            continue;
        }

        /* Miền DHT11 hỗ trợ là 0..50°C nên phần dư nhiệt độ không âm. */
        ESP_LOGI(TAG, "DHT11 temperature=%d.%d C humidity=%u.%u %%RH",
                 (int)(sample.temperature_x10 / 10),
                 (int)(sample.temperature_x10 % 10),
                 (unsigned)(sample.humidity_x10 / 10U),
                 (unsigned)(sample.humidity_x10 % 10U));
    }
}
