#include "esp_log.h"
#include "node_config.h"
#include "node_types.h"

static const char *TAG = "node";

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
