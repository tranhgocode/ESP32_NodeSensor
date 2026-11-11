/*
 * Bài thử LED thường GPIO15, Light-sleep và bộ đếm trong RAM.
 * Chạy độc lập khi chưa nối Ra-02, không truy cập radio hoặc đọc DHT11.
 * Sau khi ngủ đủ 5 giây, bật LED 100 ms rồi tắt và tăng ID/Seq một lần.
 * Bộ điều phối cảm biến/radio chưa được ghép vào bài thử này.
 */
#include <inttypes.h>

#include "app/node_state.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "indicator/status_led.h"
#include "node_config.h"
#include "power/node_power.h"

#define BENCH_LED_PULSE_MS 100U

static const char *TAG = "power_bench";

void app_main(void)
{
    const node_config_t config = NODE_CONFIG_DEFAULT;
    node_context_t ctx;
    node_state_init(&ctx, &config);
    ESP_LOGI(TAG, "LED GPIO%d HIGH=on, Light-sleep %lu ms, LED/sleep only, no radio required",
             BOARD_STATUS_LED_GPIO, (unsigned long)config.sleep_ms);
    ESP_LOGI(TAG, "Boot ID=%u Seq=%u", (unsigned)ctx.id, (unsigned)ctx.seq);

    const node_result_t led_init = status_led_init();
    if (led_init != NODE_OK)
    {
        ESP_LOGE(TAG, "LED init failed: %d, bench stopped", (int)led_init);
        return;
    }
    for (;;)
    {
        node_result_t result = status_led_off();
        const uint8_t saved_id = ctx.id;
        const uint16_t saved_seq = ctx.seq;
        const int64_t started_us = esp_timer_get_time();
        if (result == NODE_OK)
        {
            result = node_power_sleep_ms(config.sleep_ms);
        }
        if (result != NODE_OK)
        {
            /* Đây chỉ là khoảng chờ khi lỗi, không phải Light-sleep thành công.
             * Không tăng bộ đếm, đo hoặc phát dữ liệu sau khi ngủ thất bại. */
            ESP_LOGE(TAG, "LED preparation/MCU sleep failed: %d, backoff %lu ms",
                     (int)result, (unsigned long)config.sleep_ms);
            vTaskDelay(pdMS_TO_TICKS(config.sleep_ms));
            continue;
        }
        ESP_LOGI(TAG, "Wake total_us=%" PRId64 " ID=%u Seq=%u RAM=%s",
                 esp_timer_get_time() - started_us, (unsigned)ctx.id,
                 (unsigned)ctx.seq,
                 ctx.id == saved_id && ctx.seq == saved_seq ? "preserved" : "MISMATCH");
        if (ctx.id != saved_id || ctx.seq != saved_seq)
        {
            ESP_LOGE(TAG, "RAM counter mismatch, bench stopped");
            return;
        }
        result = status_led_set(1, 0, 0);
        if (result == NODE_OK)
        {
            vTaskDelay(pdMS_TO_TICKS(BENCH_LED_PULSE_MS));
        }
        const node_result_t off_result = status_led_off();
        if (result != NODE_OK || off_result != NODE_OK)
        {
            ESP_LOGE(TAG, "LED pulse failed: set=%d off=%d, backoff",
                     (int)result, (int)off_result);
            vTaskDelay(pdMS_TO_TICKS(config.sleep_ms));
            continue;
        }
        ctx.state = NODE_STATE_MEASURE; /* Chỉ đánh dấu lượt thử, không thực hiện đo cảm biến. */
        node_state_finish_cycle(&ctx);
        ESP_LOGI(TAG, "Bench finished: next ID=%u Seq=%u", (unsigned)ctx.id,
                 (unsigned)ctx.seq);
    }
}
