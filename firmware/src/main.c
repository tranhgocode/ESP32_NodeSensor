/*
 * Luồng khởi động hiện tại chỉ tạo cấu hình mặc định và ghi các giá trị đó
 * một lần. Đọc được timeout trong log chưa có nghĩa là chu kỳ ngủ, đo DHT,
 * truyền LoRa hay chờ ACK đã được triển khai.
 */
#include "esp_log.h"
#include "node_config.h"
#include "node_types.h"

static const char *TAG = "node";

/**
 * Điểm vào do ESP-IDF gọi khi ứng dụng bắt đầu.
 *
 * Hàm không nhận tham số và không trả về giá trị. Cấu hình là bản sao cục bộ
 * của NODE_CONFIG_DEFAULT; sau ba dòng log hàm kết thúc mà không khởi tạo
 * ngoại vi hay bắt đầu một vòng lặp lấy mẫu.
 */
void app_main(void)
{
    const node_config_t config = NODE_CONFIG_DEFAULT;

    ESP_LOGI(TAG, "Firmware foundation ready (Task 1)");
    ESP_LOGI(TAG, "Node=0x%02X gateway=0x%02X", (unsigned)config.node_addr,
             (unsigned)config.gateway_addr);
    ESP_LOGI(TAG, "Configured sleep=%u ms TX timeout=%u ms ACK timeout=%u ms",
             (unsigned)config.sleep_ms, (unsigned)config.tx_timeout_ms,
             (unsigned)config.ack_timeout_ms);
}
