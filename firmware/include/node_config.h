#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <stdint.h>

/*
 * Các mặc định dùng để tạo node_config_t. Bài thử Task 4 dùng sleep_ms làm
 * khoảng vTaskDelay giữa các lần đọc DHT11; radio, ACK và Light-sleep chưa
 * được ghép vào runtime. Các hằng radio phía dưới vẫn là cấu hình dự kiến.
 */
#define NODE_DEFAULT_ADDR 0x02U
#define NODE_GATEWAY_ADDR 0x00U
#define NODE_SLEEP_INTERVAL_MS 5000U
#define NODE_TX_TIMEOUT_MS 3000U
#define NODE_ACK_TIMEOUT_MS 1000U

/* Tham số dự kiến cho radio SX127x, chưa được áp dụng trong app_main(). */
#define NODE_RADIO_FREQUENCY_HZ 433000000U
#define NODE_RADIO_TX_POWER_DBM 17
#define NODE_RADIO_SPREADING_FACTOR 7U
#define NODE_RADIO_BANDWIDTH_HZ 125000U
#define NODE_RADIO_CODING_RATE_DENOMINATOR 5U
#define NODE_RADIO_SYNC_WORD 0x34U
#define NODE_RADIO_HARDWARE_CRC_ENABLED 1

/**
 * Giá trị cấu hình dùng chung giữa phần khởi động và các module node.
 * Chân GPIO chưa có trong cấu trúc này; cần xác nhận sơ đồ và dây nối trước
 * khi gán chân cho sensor hoặc radio.
 */
typedef struct
{
    uint8_t node_addr;       /**< Địa chỉ nguồn của node. */
    uint8_t gateway_addr;    /**< Địa chỉ đích mặc định của gateway. */
    uint32_t sleep_ms;       /**< Thời gian ngủ dự kiến, tính bằng mili giây. */
    uint32_t tx_timeout_ms;  /**< Giới hạn chờ thao tác phát, tính bằng mili giây. */
    uint32_t ack_timeout_ms; /**< Giới hạn chờ ACK, tính bằng mili giây. */
} node_config_t;

/* Khởi tạo đủ các trường cấu hình bằng hằng số ở trên; không cấp phát tài nguyên. */
#define NODE_CONFIG_DEFAULT                          \
    {                                                \
        .node_addr = NODE_DEFAULT_ADDR,              \
        .gateway_addr = NODE_GATEWAY_ADDR,           \
        .sleep_ms = NODE_SLEEP_INTERVAL_MS,          \
        .tx_timeout_ms = NODE_TX_TIMEOUT_MS,         \
        .ack_timeout_ms = NODE_ACK_TIMEOUT_MS,       \
    }

#endif
