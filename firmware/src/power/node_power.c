#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "power/node_power.h"

#define MICROSECONDS_PER_MILLISECOND INT64_C(1000)

static const char *TAG = "node_power";

static node_result_t finish_sleep(node_result_t result)
{
    /* Chỉ tắt bộ định thời do module sở hữu, giữ nguyên các nguồn đánh thức khác. */
    const esp_err_t cleanup = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    if (cleanup != ESP_OK)
    {
        ESP_LOGE(TAG, "Timer wakeup cleanup failed: %d", (int)cleanup);
        return NODE_IO_ERROR;
    }
    return result;
}

node_result_t node_power_sleep_ms(uint32_t duration_ms)
{
    if (duration_ms == 0U)
    {
        return NODE_OK;
    }

    const int64_t started_us = esp_timer_get_time();
    const int64_t duration_us = (int64_t)duration_ms * MICROSECONDS_PER_MILLISECOND;
    if (started_us < 0 || started_us > INT64_MAX - duration_us)
    {
        return NODE_OUT_OF_RANGE;
    }
    const int64_t deadline_us = started_us + duration_us;
    int64_t now_us = started_us;
    bool timer_armed = false;

    while (now_us < deadline_us)
    {
        /* Tính cả thời gian ghi log hoặc bị nhường CPU kể từ lần thức trước. */
        const int64_t current_us = esp_timer_get_time();
        if (current_us < now_us)
        {
            return timer_armed ? finish_sleep(NODE_IO_ERROR) : NODE_IO_ERROR;
        }
        now_us = current_us;
        if (now_us >= deadline_us)
        {
            break;
        }
        const esp_err_t timer_result = esp_sleep_enable_timer_wakeup(
            (uint64_t)(deadline_us - now_us));
        if (timer_result != ESP_OK)
        {
            ESP_LOGE(TAG, "Timer wakeup setup failed: %d", (int)timer_result);
            return timer_armed ? finish_sleep(NODE_IO_ERROR) : NODE_IO_ERROR;
        }
        timer_armed = true;
        const esp_err_t sleep_result = esp_light_sleep_start();
        if (sleep_result != ESP_OK)
        {
            ESP_LOGE(TAG, "Light-sleep failed: %d", (int)sleep_result);
            return finish_sleep(NODE_IO_ERROR);
        }

        /* ESP-IDF 6.1 trả mặt nạ bit để ghi nhận nhiều nguồn đánh thức đồng thời.
         * Thức do bộ định thời chưa đủ để kết luận đã ngủ tới hạn. */
        const uint32_t causes = esp_sleep_get_wakeup_causes();
        const int64_t woke_us = esp_timer_get_time();
        ESP_LOGI(TAG, "Wake causes=0x%08lx elapsed_us=%lld",
                 (unsigned long)causes, (long long)(woke_us - started_us));
        if (woke_us <= now_us)
        {
            ESP_LOGE(TAG, "Light-sleep made no monotonic progress");
            return finish_sleep(NODE_IO_ERROR);
        }
        now_us = woke_us;
        /* Nếu thức sớm, chỉ ngủ tiếp phần thời gian còn thiếu tới hạn ban đầu. */
    }
    return timer_armed ? finish_sleep(NODE_OK) : NODE_OK;
}
