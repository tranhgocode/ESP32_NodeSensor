#include "platform/node_platform.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "board_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "indicator/status_led.h"
#include "power/node_power.h"
#include "radio/node_radio.h"
#include "sensor/dht11.h"

#define TRACE_EVENT_LIMIT 8U
#define TRACE_MESSAGE_SIZE 48U
#define HEX_CHARACTERS_PER_BYTE 3U

typedef struct
{
    int64_t at_us;
    node_result_t result;
    char message[TRACE_MESSAGE_SIZE];
    uint8_t frame[NODE_PACKET_MAX_SIZE];
    size_t len;
} trace_event_t;

/* Chỉ task ứng dụng sở hữu, không ghi từ ISR hoặc task driver khác. */
static trace_event_t trace_events[TRACE_EVENT_LIMIT];
static size_t trace_count;
static uint32_t trace_dropped;
static const char *TAG = "node_platform";

static const char *result_name(node_result_t result)
{
    switch (result)
    {
        case NODE_OK: return "OK";
        case NODE_INVALID_ARG: return "INVALID_ARG";
        case NODE_INVALID_FRAME: return "INVALID_FRAME";
        case NODE_BUFFER_TOO_SMALL: return "BUFFER_TOO_SMALL";
        case NODE_INIT_FAILED: return "INIT_FAILED";
        case NODE_TIMEOUT: return "TIMEOUT";
        case NODE_CHECKSUM_ERROR: return "CHECKSUM_ERROR";
        case NODE_OUT_OF_RANGE: return "OUT_OF_RANGE";
        case NODE_IO_ERROR: return "IO_ERROR";
        case NODE_ENCODE_FAILED: return "ENCODE_FAILED";
        default: return "UNKNOWN";
    }
}

static void platform_trace(const char *message, node_result_t result,
                            const uint8_t *frame, size_t len)
{
    if (trace_count == TRACE_EVENT_LIMIT)
    {
        if (trace_dropped != UINT32_MAX)
        {
            ++trace_dropped;
        }
        return;
    }
    trace_event_t *entry = &trace_events[trace_count++];
    entry->at_us = esp_timer_get_time();
    entry->result = result;
    size_t copied = 0;
    if (message != NULL)
    {
        while (copied < sizeof(entry->message) - 1 && message[copied] != '\0')
        {
            entry->message[copied] = message[copied];
            ++copied;
        }
    }
    entry->message[copied] = '\0';
    entry->len = frame == NULL ? 0 : (len > sizeof(entry->frame) ? sizeof(entry->frame) : len);
    if (entry->len != 0)
    {
        memcpy(entry->frame, frame, entry->len);
    }
}

static node_result_t platform_radio_send(const uint8_t *frame, size_t len,
                                         uint32_t timeout_ms, int64_t *tx_done_us)
{
    const node_result_t result = node_radio_send(frame, len, timeout_ms, tx_done_us);
    /* Send đã chuyển sang RX trước khi trả OK, ở đây chỉ copy trace vào RAM. */
    platform_trace("TX completed", result, NULL, 0);
    return result;
}

static node_result_t platform_sleep_ms(uint32_t duration_ms)
{
    const node_result_t result = status_led_off();
    if (result != NODE_OK)
    {
        return result;
    }
    /* Đẩy hết stdio rồi chờ console truyền trước khi Light-sleep ngắt USB. */
    if (fflush(stdout) != 0 || fsync(STDOUT_FILENO) != 0)
    {
        return NODE_IO_ERROR;
    }
    return node_power_sleep_ms(duration_ms);
}

static const node_io_t PLATFORM_IO = {
    .sensor_init = dht11_init,
    .radio_init = node_radio_init,
    .sensor_read = dht11_read,
    .radio_send = platform_radio_send,
    .radio_receive = node_radio_receive,
    .radio_sleep = node_radio_sleep,
    .sleep_ms = platform_sleep_ms,
    .encode_packet = node_packet_encode,
    .now_us = esp_timer_get_time,
    .trace = platform_trace,
};

const node_io_t *node_platform_io(void)
{
    return &PLATFORM_IO;
}

void node_platform_log_config(const node_config_t *config)
{
    if (config == NULL)
    {
        ESP_LOGE(TAG, "Missing node configuration");
        return;
    }
    ESP_LOGI(TAG, "Node=0x%02X gateway=0x%02X sleep_ms=%" PRIu32
             " tx_timeout_ms=%" PRIu32 " ack_timeout_ms=%" PRIu32,
             (unsigned)config->node_addr, (unsigned)config->gateway_addr,
             config->sleep_ms, config->tx_timeout_ms, config->ack_timeout_ms);
    ESP_LOGI(TAG, "DHT11 GPIO%d, LED GPIO%d HIGH=on, radio CS%d MOSI%d SCK%d MISO%d DIO0=%d RESET=%d",
             BOARD_DHT11_GPIO, BOARD_STATUS_LED_GPIO, BOARD_RADIO_CS_GPIO,
             BOARD_RADIO_MOSI_GPIO, BOARD_RADIO_SCK_GPIO, BOARD_RADIO_MISO_GPIO,
             BOARD_RADIO_DIO0_GPIO, BOARD_RADIO_RESET_GPIO);
    ESP_LOGI(TAG, "LoRa Hz=%u power_dbm=%d SF=%u BW=%u CR=4/%u sync=0x%02X",
             (unsigned)NODE_RADIO_FREQUENCY_HZ, NODE_RADIO_TX_POWER_DBM,
             (unsigned)NODE_RADIO_SPREADING_FACTOR, (unsigned)NODE_RADIO_BANDWIDTH_HZ,
             (unsigned)NODE_RADIO_CODING_RATE_DENOMINATOR, (unsigned)NODE_RADIO_SYNC_WORD);
    ESP_LOGI(TAG, "LoRa hardware_crc=%u preamble=%u explicit_header=%u inverted_iq=%u",
             (unsigned)NODE_RADIO_HARDWARE_CRC_ENABLED, (unsigned)NODE_RADIO_PREAMBLE_SYMBOLS,
             (unsigned)NODE_RADIO_EXPLICIT_HEADER_ENABLED, (unsigned)NODE_RADIO_INVERT_IQ_ENABLED);
}

static void log_frame(const trace_event_t *entry)
{
    static const char HEX[] = "0123456789ABCDEF";
    char hex[NODE_PACKET_MAX_SIZE * HEX_CHARACTERS_PER_BYTE];
    for (size_t i = 0; i < entry->len; ++i)
    {
        hex[i * HEX_CHARACTERS_PER_BYTE] = HEX[entry->frame[i] >> 4];
        hex[i * HEX_CHARACTERS_PER_BYTE + 1] = HEX[entry->frame[i] & 0x0F];
        hex[i * HEX_CHARACTERS_PER_BYTE + 2] = i + 1 < entry->len ? ' ' : '\0';
    }
    ESP_LOGI(TAG, "Frame len=%u hex=%s", (unsigned)entry->len, hex);
    node_packet_t packet;
    if (node_packet_decode(entry->frame, entry->len, &packet) != NODE_OK)
    {
        return;
    }
    ESP_LOGI(TAG, "Packet type=0x%02X src=0x%02X dest=0x%02X ID=%u Seq=%u",
             (unsigned)packet.type, (unsigned)packet.src, (unsigned)packet.dest,
             (unsigned)packet.id, (unsigned)packet.seq);
    if (packet.type == NODE_PACKET_TYPE_DATA)
    {
        const uint16_t raw_temperature = ((uint16_t)packet.payload[0] << 8) | packet.payload[1];
        const int32_t temperature = raw_temperature < 0x8000U ?
            raw_temperature : (int32_t)raw_temperature - INT32_C(65536);
        const uint16_t humidity = ((uint16_t)packet.payload[2] << 8) | packet.payload[3];
        ESP_LOGI(TAG, "DATA temperature_x10=%" PRId32 " humidity_x10=%u",
                 temperature, (unsigned)humidity);
    }
    else if (packet.type == NODE_PACKET_TYPE_ERROR)
    {
        ESP_LOGW(TAG, "ERROR code=0x%02X", (unsigned)packet.payload[0]);
    }
}

void node_platform_flush_trace(void)
{
    for (size_t i = 0; i < trace_count; ++i)
    {
        const trace_event_t *entry = &trace_events[i];
        if (entry->result == NODE_OK)
        {
            ESP_LOGI(TAG, "at_us=%" PRId64 " %s result=%s(%d)", entry->at_us,
                     entry->message, result_name(entry->result), (int)entry->result);
        }
        else
        {
            ESP_LOGW(TAG, "at_us=%" PRId64 " %s result=%s(%d)", entry->at_us,
                     entry->message, result_name(entry->result), (int)entry->result);
        }
        if (entry->len != 0)
        {
            log_frame(entry);
        }
    }
    if (trace_dropped != 0)
    {
        ESP_LOGW(TAG, "Trace capacity reached, dropped=%" PRIu32, trace_dropped);
    }
    trace_count = 0;
    trace_dropped = 0;
}
