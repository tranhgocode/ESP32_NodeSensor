/*
 * Bench radio: luân phiên DATA mẫu 25°C/60%RH và ERROR đọc lỗi,
 * gửi một lần mỗi lượt, chờ ACK đúng giao dịch tới deadline cũ. Dữ liệu này
 * là tổng hợp để kiểm tra byte/radio, không đọc DHT11 hoặc chứng minh sensor.
 * Ứng dụng đầy đủ sẽ thay bench bằng phần điều phối I/O.
 */
#include <inttypes.h>

#include "app/node_state.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "node_config.h"
#include "protocol/node_packet.h"
#include "protocol/node_payload.h"
#include "radio/node_radio.h"

#define BENCH_READ_ERROR_CODE 0x02U
#define HEX_DIGITS_PER_BYTE 2U

static const char *TAG = "radio_bench";

static void log_frame(const char *direction, const uint8_t *frame, size_t len)
{
    static const char hex[] = "0123456789ABCDEF";
    char text[NODE_PACKET_MAX_SIZE * HEX_DIGITS_PER_BYTE + 1U];
    if (len > NODE_PACKET_MAX_SIZE)
    {
        return;
    }
    for (size_t i = 0; i < len; ++i)
    {
        text[HEX_DIGITS_PER_BYTE * i] = hex[frame[i] >> 4];
        text[HEX_DIGITS_PER_BYTE * i + 1U] = hex[frame[i] & 0x0FU];
    }
    text[len * HEX_DIGITS_PER_BYTE] = '\0';
    ESP_LOGI(TAG, "%s len=%u %s", direction, (unsigned)len, text);
}

static node_result_t encode_bench_frame(const node_context_t *ctx,
                                        uint8_t *frame, size_t *written)
{
    const bool error_frame = (ctx->seq & 1U) != 0;
    node_packet_t packet = {
        .type = error_frame ? NODE_PACKET_TYPE_ERROR : NODE_PACKET_TYPE_DATA,
        .src = ctx->node_addr,
        .dest = ctx->gateway_addr,
        .id = ctx->id,
        .len = error_frame ? 1U : 4U,
        .seq = ctx->seq,
    };
    const node_measurement_t synthetic_sample = {.temperature_x10 = 250, .humidity_x10 = 600};
    const node_result_t payload_result = error_frame ?
        node_payload_encode_error(BENCH_READ_ERROR_CODE, packet.payload) :
        node_payload_encode_data(&synthetic_sample, packet.payload);
    if (payload_result != NODE_OK)
    {
        return payload_result;
    }
    return node_packet_encode(&packet, frame, NODE_PACKET_MAX_SIZE, written);
}

static node_result_t wait_bench_ack(const node_context_t *ctx)
{
    uint8_t frame[NODE_PACKET_MAX_SIZE];
    while (esp_timer_get_time() < ctx->ack_deadline_us)
    {
        size_t len = 0;
        const node_result_t result = node_radio_receive(frame, sizeof(frame), &len,
                                                       ctx->ack_deadline_us);
        if (result != NODE_OK)
        {
            return result;
        }
        node_packet_t packet;
        const node_result_t decode_result = node_packet_decode(frame, len, &packet);
        if (decode_result == NODE_OK &&
            node_state_accept_ack(ctx, &packet, esp_timer_get_time()))
        {
            log_frame("RX ACK", frame, len);
            return NODE_OK;
        }
        /* Log ngoài đoạn TX_DONE→RX, packet sai không tạo deadline mới. */
        ESP_LOGW(TAG, "Ignored RX packet: decode=%d ID=%u Seq=%u",
                 (int)decode_result, (unsigned)ctx->id, (unsigned)ctx->seq);
    }
    return NODE_TIMEOUT;
}

static void run_bench_round(node_context_t *ctx, const node_config_t *config)
{
    uint8_t frame[NODE_PACKET_MAX_SIZE];
    size_t len = 0;
    ctx->state = NODE_STATE_TRANSMIT;
    const node_result_t encode_result = encode_bench_frame(ctx, frame, &len);
    if (encode_result != NODE_OK)
    {
        ESP_LOGE(TAG, "Bench encode failed: %d", (int)encode_result);
        return;
    }
    int64_t tx_done_us = 0;
    const node_result_t tx_result = node_radio_send(frame, len, config->tx_timeout_ms,
                                                   &tx_done_us);
    if (tx_result != NODE_OK)
    {
        ESP_LOGW(TAG, "TX failed: result=%d ID=%u Seq=%u; no retry",
                 (int)tx_result, (unsigned)ctx->id, (unsigned)ctx->seq);
        return;
    }
    /* send đã bật RX trước khi trở về, ACK đến trong lúc log vẫn được giữ.
     * Chốt deadline từ timestamp ISR, không từ thời điểm sau log UART. */
    node_state_begin_ack_wait(ctx, tx_done_us);
    log_frame("TX synthetic", frame, len);
    ESP_LOGI(TAG, "TX_DONE=%" PRId64 " ACK deadline=%" PRId64,
             tx_done_us, ctx->ack_deadline_us);
    const node_result_t ack_result = wait_bench_ack(ctx);
    ESP_LOGI(TAG, "ACK result=%d ID=%u Seq=%u", (int)ack_result,
             (unsigned)ctx->id, (unsigned)ctx->seq);
}

void app_main(void)
{
    const node_config_t config = NODE_CONFIG_DEFAULT;
    node_context_t ctx;
    node_state_init(&ctx, &config);
    ESP_LOGI(TAG, "Task 5 radio bench: synthetic DATA/ERROR, no DHT11 reads");
    ESP_LOGI(TAG, "CS=%d MOSI=%d SCK=%d MISO=%d DIO0=%d RESET=%d",
             BOARD_RADIO_CS_GPIO, BOARD_RADIO_MOSI_GPIO, BOARD_RADIO_SCK_GPIO,
             BOARD_RADIO_MISO_GPIO, BOARD_RADIO_DIO0_GPIO, BOARD_RADIO_RESET_GPIO);
    ESP_LOGI(TAG, "LoRa=%u Hz %d dBm SF%u BW%u CR4/%u Sync=0x%02X preamble=%u explicit, normal IQ",
             (unsigned)NODE_RADIO_FREQUENCY_HZ, NODE_RADIO_TX_POWER_DBM,
             (unsigned)NODE_RADIO_SPREADING_FACTOR, (unsigned)NODE_RADIO_BANDWIDTH_HZ,
             (unsigned)NODE_RADIO_CODING_RATE_DENOMINATOR, NODE_RADIO_SYNC_WORD,
             (unsigned)NODE_RADIO_PREAMBLE_SYMBOLS);
    const node_result_t init_result = node_radio_init();
    if (init_result != NODE_OK)
    {
        ESP_LOGE(TAG, "Radio init failed: %d", (int)init_result);
        return;
    }
    for (;;)
    {
        /* Radio ngủ trong khoảng nghỉ, MCU vẫn chỉ dùng vTaskDelay. Đây chưa
         * là Light-sleep hoặc chu kỳ sensor/ERROR thật. */
        const node_result_t sleep_result = node_radio_sleep();
        if (sleep_result != NODE_OK)
        {
            ESP_LOGE(TAG, "Radio sleep failed: %d; bench stopped", (int)sleep_result);
            node_radio_deinit();
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(config.sleep_ms));
        run_bench_round(&ctx, &config);
        node_state_finish_cycle(&ctx);
    }
}
