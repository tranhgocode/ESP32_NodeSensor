#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <stdint.h>

#define NODE_DEFAULT_ADDR 0x02U
#define NODE_GATEWAY_ADDR 0x00U
#define NODE_SLEEP_INTERVAL_MS 5000U
#define NODE_TX_TIMEOUT_MS 3000U
#define NODE_ACK_TIMEOUT_MS 1000U

#define NODE_RADIO_FREQUENCY_HZ 433000000U
#define NODE_RADIO_TX_POWER_DBM 17
#define NODE_RADIO_SPREADING_FACTOR 7U
#define NODE_RADIO_BANDWIDTH_HZ 125000U
#define NODE_RADIO_CODING_RATE_DENOMINATOR 5U
#define NODE_RADIO_SYNC_WORD 0x34U
#define NODE_RADIO_HARDWARE_CRC_ENABLED 1

// GPIO assignments are intentionally deferred until the wiring is confirmed.
typedef struct
{
    uint8_t node_addr;
    uint8_t gateway_addr;
    uint32_t sleep_ms;
    uint32_t tx_timeout_ms;
    uint32_t ack_timeout_ms;
} node_config_t;

#define NODE_CONFIG_DEFAULT                          \
    {                                                \
        .node_addr = NODE_DEFAULT_ADDR,              \
        .gateway_addr = NODE_GATEWAY_ADDR,           \
        .sleep_ms = NODE_SLEEP_INTERVAL_MS,          \
        .tx_timeout_ms = NODE_TX_TIMEOUT_MS,         \
        .ack_timeout_ms = NODE_ACK_TIMEOUT_MS,       \
    }

#endif
