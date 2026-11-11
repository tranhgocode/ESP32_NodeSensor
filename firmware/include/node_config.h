#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <stdint.h>

/*
 * Các mặc định để tạo node_config_t cho ứng dụng node tích hợp.
 * Main nối bộ điều phối với driver thật, ngủ trước mỗi lần đo và gửi.
 */
#define NODE_DEFAULT_ADDR 0x02U
#define NODE_GATEWAY_ADDR 0x00U
#define NODE_SLEEP_INTERVAL_MS 5000U
#define NODE_TX_TIMEOUT_MS 3000U
#define NODE_ACK_TIMEOUT_MS 1000U

/* Profile SX1278 áp dụng trong node_radio, gateway đã chốt preamble/header/IQ. */
#define NODE_RADIO_FREQUENCY_HZ 433000000U
#define NODE_RADIO_TX_POWER_DBM 17
#define NODE_RADIO_SPREADING_FACTOR 7U
#define NODE_RADIO_BANDWIDTH_HZ 125000U
#define NODE_RADIO_CODING_RATE_DENOMINATOR 5U
#define NODE_RADIO_SYNC_WORD 0x34U
#define NODE_RADIO_HARDWARE_CRC_ENABLED 1
#define NODE_RADIO_PREAMBLE_SYMBOLS 8U
#define NODE_RADIO_EXPLICIT_HEADER_ENABLED 1
#define NODE_RADIO_INVERT_IQ_ENABLED 0
#define NODE_RADIO_SPI_CLOCK_HZ 8000000U

/**
 * Giá trị cấu hình dùng chung giữa phần khởi động và các module node.
 * Pin map tách riêng ở board_config.h, cấu trúc này không sở hữu handle
 * hoặc tài nguyên ngoại vi nên có thể dùng chung trong portable test.
 */
typedef struct
{
    uint8_t node_addr;       /**< Địa chỉ nguồn của node. */
    uint8_t gateway_addr;    /**< Địa chỉ đích mặc định của gateway. */
    uint32_t sleep_ms;       /**< Thời gian ngủ dự kiến, tính bằng mili giây. */
    uint32_t tx_timeout_ms;  /**< Giới hạn chờ thao tác phát, tính bằng mili giây. */
    uint32_t ack_timeout_ms; /**< Giới hạn chờ ACK, tính bằng mili giây. */
} node_config_t;

/* Khởi tạo đủ các trường cấu hình bằng hằng số ở trên, không cấp phát tài nguyên. */
#define NODE_CONFIG_DEFAULT                          \
    {                                                \
        .node_addr = NODE_DEFAULT_ADDR,              \
        .gateway_addr = NODE_GATEWAY_ADDR,           \
        .sleep_ms = NODE_SLEEP_INTERVAL_MS,          \
        .tx_timeout_ms = NODE_TX_TIMEOUT_MS,         \
        .ack_timeout_ms = NODE_ACK_TIMEOUT_MS,       \
    }

#endif
