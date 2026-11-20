#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/* Cấu hình mặc định của node. */
#define NODE_DEFAULT_ADDR 0x02U             /* Địa chỉ mặc định của node. */
#define NODE_GATEWAY_ADDR 0x00U             /* Địa chỉ gateway. */
#ifndef NODE_PM25_FORECAST_DEMO
#define NODE_PM25_FORECAST_DEMO 0
#endif
#if NODE_PM25_FORECAST_DEMO != 0 && NODE_PM25_FORECAST_DEMO != 1
#error "NODE_PM25_FORECAST_DEMO must be 0 or 1"
#endif
#ifndef NODE_MEASUREMENT_INTERVAL_MS
#if NODE_PM25_FORECAST_DEMO
#define NODE_MEASUREMENT_INTERVAL_MS 120000U
#else
#define NODE_MEASUREMENT_INTERVAL_MS 60000U /* Chu kỳ đo. */
#endif
#endif
#define NODE_TX_TIMEOUT_MS 3000U            /* Thời gian chờ phát. */
#define NODE_ACK_TIMEOUT_MS 1000U           /* Thời gian chờ ACK. */

/* Chỉ chọn một cảm biến nhiệt độ và độ ẩm. */
#define NODE_TEMP_HUM_SENSOR_DHT11 1
#define NODE_TEMP_HUM_SENSOR_SHT30 2
#ifndef NODE_TEMP_HUM_SENSOR
#define NODE_TEMP_HUM_SENSOR NODE_TEMP_HUM_SENSOR_DHT11
#endif
#if NODE_TEMP_HUM_SENSOR != NODE_TEMP_HUM_SENSOR_DHT11 && \
    NODE_TEMP_HUM_SENSOR != NODE_TEMP_HUM_SENSOR_SHT30
#error "NODE_TEMP_HUM_SENSOR must select DHT11 or SHT30"
#endif

#ifndef NODE_SHT30_I2C_ADDRESS
#define NODE_SHT30_I2C_ADDRESS 0x44U
#endif
#if NODE_SHT30_I2C_ADDRESS != 0x44 && NODE_SHT30_I2C_ADDRESS != 0x45
#error "NODE_SHT30_I2C_ADDRESS must be 0x44 or 0x45"
#endif

/* 0 giữ SET ở mức thấp. 1 bật HM3301 và thêm PM2.5 vào DATA. */
#ifndef NODE_HM3301_ENABLED
#define NODE_HM3301_ENABLED 1
#endif
#if NODE_HM3301_ENABLED != 0 && NODE_HM3301_ENABLED != 1
#error "NODE_HM3301_ENABLED must be 0 or 1"
#endif
#define NODE_HM3301_WARMUP_MS 30000U     /* Thời gian làm nóng HM3301. */
#define NODE_HM3301_MIN_WARMUP_MS 30000U /* Thời gian làm nóng tối thiểu. */

#ifndef NODE_PM25_FORECAST_ENABLED
#define NODE_PM25_FORECAST_ENABLED NODE_HM3301_ENABLED
#endif
#if NODE_PM25_FORECAST_ENABLED != 0 && NODE_PM25_FORECAST_ENABLED != 1
#error "NODE_PM25_FORECAST_ENABLED must be 0 or 1"
#endif
#if NODE_PM25_FORECAST_ENABLED && !NODE_HM3301_ENABLED
#error "PM2.5 forecasting requires HM3301"
#endif
#define NODE_PM25_DEMO_AWAKE_MS 60000U
#if NODE_PM25_FORECAST_DEMO && (!NODE_PM25_FORECAST_ENABLED || \
    NODE_MEASUREMENT_INTERVAL_MS <= NODE_PM25_DEMO_AWAKE_MS + NODE_HM3301_WARMUP_MS)
#error "PM2.5 demo requires forecast enabled and time for awake hold and warmup"
#endif

/* Bộ chia áp 1/2: pin 4200 mV cho 2100 mV tại ADC. */
#define NODE_BATTERY_DIVIDER_NUMERATOR 2U
#define NODE_BATTERY_DIVIDER_DENOMINATOR 1U
#define NODE_BATTERY_SAMPLE_COUNT 16U /* Số mẫu điện áp pin. */

/* Gateway phải dùng cùng cấu hình LoRa. */
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

/** Cấu hình dùng chung, sơ đồ chân nằm trong board_config.h. */
typedef struct
{
    uint8_t node_addr;                /**< Địa chỉ nguồn của node. */
    uint8_t gateway_addr;             /**< Địa chỉ gateway, đích của gói do node gửi. */
    uint32_t measurement_interval_ms; /**< Khoảng cách giữa hai mốc đo, tính bằng ms. */
    bool hm3301_enabled;              /**< Bật đo và gửi PM2.5. */
    uint32_t hm3301_warmup_ms;        /**< Thời gian làm nóng sau khi SET lên mức cao. */
    uint32_t tx_timeout_ms;           /**< Giới hạn chờ phát, tính bằng ms. */
    uint32_t ack_timeout_ms;          /**< Giới hạn chờ ACK, tính bằng ms. */
} node_config_t;

/* Giá trị mặc định cho node_config_t. */
#define NODE_CONFIG_DEFAULT                                      \
    {                                                            \
        .node_addr = NODE_DEFAULT_ADDR,                          \
        .gateway_addr = NODE_GATEWAY_ADDR,                       \
        .measurement_interval_ms = NODE_MEASUREMENT_INTERVAL_MS, \
        .hm3301_enabled = NODE_HM3301_ENABLED,                   \
        .hm3301_warmup_ms = NODE_HM3301_WARMUP_MS,               \
        .tx_timeout_ms = NODE_TX_TIMEOUT_MS,                     \
        .ack_timeout_ms = NODE_ACK_TIMEOUT_MS,                   \
    }

#endif
