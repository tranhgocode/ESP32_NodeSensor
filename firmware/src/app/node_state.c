#include "app/node_state.h"

#define NODE_MICROSECONDS_PER_MILLISECOND INT64_C(1000)

void node_state_init(node_context_t *ctx, const node_config_t *config)
{
    if (ctx == NULL || config == NULL)
    {
        return;
    }

    /* Khởi tạo cả context bằng giá trị, không giữ con trỏ tới config của caller.
     * Các trường không chỉ định nhận 0, kể cả counter và mốc thời gian cũ. */
    *ctx = (node_context_t){
        .state = NODE_STATE_SLEEP,
        .node_addr = config->node_addr,
        .gateway_addr = config->gateway_addr,
        .ack_timeout_ms = config->ack_timeout_ms,
        .sensor_init_result = NODE_INIT_FAILED,
    };
}

void node_state_begin_ack_wait(node_context_t *ctx, int64_t tx_done_us)
{
    if (ctx == NULL)
    {
        return;
    }

    /* Cast trước phép nhân: timeout uint32_t theo ms có thể lớn hơn miền
     * 32 bit sau khi đổi sang us, nhưng vẫn luôn biểu diễn được bằng int64_t. */
    const int64_t timeout_us = (int64_t)ctx->ack_timeout_ms *
                               NODE_MICROSECONDS_PER_MILLISECOND;
    if (tx_done_us < 0 || tx_done_us > INT64_MAX - timeout_us)
    {
        /* Không mở cửa sổ từ một clock sai hoặc để phép cộng signed bị tràn.
         * Đóng cả cửa sổ cũ; app vẫn kết thúc lượt và tăng counters bình thường. */
        ctx->tx_done_us = 0;
        ctx->ack_deadline_us = 0;
        ctx->state = NODE_STATE_FINISH;
        return;
    }

    ctx->tx_done_us = tx_done_us;
    ctx->ack_deadline_us = tx_done_us + timeout_us;
    ctx->state = NODE_STATE_WAIT_ACK;
}

bool node_state_accept_ack(const node_context_t *ctx,
                           const node_packet_t *packet,
                           int64_t now_us)
{
    if (ctx == NULL || packet == NULL || ctx->state != NODE_STATE_WAIT_ACK)
    {
        return false;
    }

    /* Clock được lấy lại sau decode; ACK đến đúng deadline không còn hợp lệ.
     * Gói sai chỉ trả false, tuyệt đối không tạo lại deadline từ now_us. */
    if (now_us < ctx->tx_done_us || now_us >= ctx->ack_deadline_us)
    {
        return false;
    }

    return packet->type == NODE_PACKET_TYPE_ACK &&
           packet->len == 0 &&
           packet->src == ctx->gateway_addr &&
           packet->dest == ctx->node_addr &&
           packet->id == ctx->id &&
           packet->seq == ctx->seq;
}

void node_state_finish_cycle(node_context_t *ctx)
{
    if (ctx == NULL || ctx->state == NODE_STATE_SLEEP)
    {
        return;
    }

    /* Cùng đường kết thúc cho ACK thành công, timeout, TX lỗi hoặc lỗi đo.
     * Cast về đúng độ rộng cho wrap độc lập, không dùng signed arithmetic. */
    ctx->id = (uint8_t)(ctx->id + 1U);
    ctx->seq = (uint16_t)(ctx->seq + 1U);
    ctx->tx_done_us = 0;
    ctx->ack_deadline_us = 0;
    ctx->state = NODE_STATE_SLEEP;
}
