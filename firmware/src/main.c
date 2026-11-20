/* Một tác vụ sở hữu trạng thái và các trình điều khiển. ISR radio chỉ chuyển sự kiện. */
#include "app/node_app.h"
#include "debug.h"
#include "esp_log.h"
#include "forecast/pm25_forecast.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "indicator/status_led.h"
#include "node_config.h"
#include "platform/node_platform.h"

#define NODE_ERROR_BACKOFF_MS 5000U

static const char *TAG = "sensor_node";

/** Khởi động node, chạy từng chu kỳ và in nhật ký sau khi kết thúc lượt LoRa. */
void app_main(void)
{
    debug_trace_log_init();
    const node_config_t config = NODE_CONFIG_DEFAULT;
    const node_io_t *io = node_platform_io();
    node_context_t ctx;
    node_platform_log_config(&config);

    const node_result_t led_result = status_led_init();
    if (led_result != NODE_OK)
    {
        ESP_LOGE(TAG, "LED init failed result=%d, node stopped", (int)led_result);
        return;
    }
#if NODE_PM25_FORECAST_ENABLED
    pm25_forecast_init();
#endif
    const node_result_t init_result = node_app_init(&ctx, &config, io);
    node_platform_flush_trace();
    if (init_result != NODE_OK)
    {
        ESP_LOGE(TAG, "Node init failed result=%d, node stopped", (int)init_result);
        return;
    }
    ESP_LOGI(TAG, "Node ready ID=%u Seq=%u sensor_init=%d",
             (unsigned)ctx.id, (unsigned)ctx.seq, (int)ctx.sensor_init_result);

    for (;;)
    {
        const uint8_t id = ctx.id;
        const uint16_t seq = ctx.seq;
        ESP_LOGI(TAG, "Cycle start ID=%u Seq=%u state=SLEEP", (unsigned)id, (unsigned)seq);
        const node_result_t result = node_app_run_cycle(&ctx, &config, io);
        node_platform_flush_trace();
#if NODE_PM25_FORECAST_ENABLED
        pm25_forecast_flush_log();
#endif
        ESP_LOGI(TAG, "Cycle finished ID=%u Seq=%u result=%d, next ID=%u Seq=%u state=%d",
                 (unsigned)id, (unsigned)seq, (int)result,
                 (unsigned)ctx.id, (unsigned)ctx.seq, (int)ctx.state);
#if NODE_PM25_FORECAST_DEMO
        if (ctx.id != id)
        {
            vTaskDelay(pdMS_TO_TICKS(NODE_PM25_DEMO_AWAKE_MS));
        }
#endif
        if (result != NODE_OK && result != NODE_TIMEOUT)
        {
            /* Chờ trước lượt tiếp theo khi lỗi khác timeout. */
            ESP_LOGW(TAG, "Cycle error, backoff %u ms", (unsigned)NODE_ERROR_BACKOFF_MS);
            vTaskDelay(pdMS_TO_TICKS(NODE_ERROR_BACKOFF_MS));
        }
    }
}
