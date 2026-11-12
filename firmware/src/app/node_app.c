#include "app/node_app.h"
#include "protocol/node_payload.h"

#define NODE_ERROR_SENSOR_INIT 0x01U
#define NODE_ERROR_SENSOR_READ 0x02U
#define NODE_ERROR_SENSOR_RANGE 0x03U
#define NODE_ERROR_DATA_ENCODING 0x04U
#define NODE_FRAME_OVERHEAD 9U

static bool valid_arguments(const node_context_t *ctx, const node_config_t *config,
                             const node_io_t *io)
{
    return ctx != NULL && config != NULL && io != NULL &&
           config->node_addr != 0 && config->node_addr != UINT8_MAX &&
           config->gateway_addr != UINT8_MAX && config->node_addr != config->gateway_addr &&
           config->sleep_ms != 0 && config->tx_timeout_ms != 0 && config->ack_timeout_ms != 0 &&
           io->sensor_init != NULL && io->radio_init != NULL && io->sensor_read != NULL &&
           io->battery_init != NULL && io->battery_read_mv != NULL &&
           io->radio_send != NULL && io->radio_receive != NULL && io->radio_sleep != NULL &&
           io->sleep_ms != NULL && io->encode_packet != NULL && io->now_us != NULL &&
           io->trace != NULL;
}

static node_result_t encode_frame(const node_io_t *io, const node_packet_t *packet,
                                   uint8_t *frame, size_t *len)
{
    *len = 0;
    const node_result_t result = io->encode_packet(packet, frame, NODE_PACKET_MAX_SIZE, len);
    if (result != NODE_OK)
    {
        return result;
    }
    /* Không để callback trả OK với độ dài sai rồi chuyển buffer lỗi tới radio. */
    if (*len != NODE_FRAME_OVERHEAD + packet->len || *len > NODE_PACKET_MAX_SIZE)
    {
        return NODE_ENCODE_FAILED;
    }
    return NODE_OK;
}

static uint16_t read_battery_mv(const node_context_t *ctx, const node_io_t *io)
{
    if (ctx->battery_init_result != NODE_OK)
    {
        return NODE_BATTERY_MV_UNAVAILABLE;
    }
    uint16_t millivolts = 0;
    const node_result_t result = io->battery_read_mv(&millivolts);
    if (result != NODE_OK)
    {
        io->trace("Battery read failed", result, NULL, 0);
        return NODE_BATTERY_MV_UNAVAILABLE;
    }
    return millivolts;
}

static node_result_t prepare_frame(node_context_t *ctx, const node_io_t *io,
                                    uint8_t *frame, size_t *len)
{
    node_packet_t packet = {
        .type = NODE_PACKET_TYPE_DATA,
        .src = ctx->node_addr,
        .dest = ctx->gateway_addr,
        .id = ctx->id,
        .seq = ctx->seq,
        .len = NODE_DATA_PAYLOAD_SIZE,
    };
    const uint16_t battery_mv = read_battery_mv(ctx, io);
    uint8_t error = NODE_ERROR_SENSOR_INIT;
    if (ctx->sensor_init_result == NODE_OK)
    {
        node_measurement_t sample = {0};
        const node_result_t read_result = io->sensor_read(&sample);
        if (read_result == NODE_OK)
        {
            node_result_t result = node_payload_encode_data(&sample,
                                                            battery_mv,
                                                            packet.payload);
            if (result == NODE_OK)
            {
                result = encode_frame(io, &packet, frame, len);
            }
            if (result == NODE_OK)
            {
                return NODE_OK;
            }
            io->trace("DATA encoding failed", result, NULL, 0);
            error = NODE_ERROR_DATA_ENCODING;
        }
        else
        {
            io->trace("Sensor read failed", read_result, NULL, 0);
            error = read_result == NODE_OUT_OF_RANGE ?
                NODE_ERROR_SENSOR_RANGE : NODE_ERROR_SENSOR_READ;
        }
    }

    packet.type = NODE_PACKET_TYPE_ERROR;
    packet.len = 1;
    node_result_t result = node_payload_encode_error(error, packet.payload);
    if (result == NODE_OK)
    {
        result = encode_frame(io, &packet, frame, len);
    }
    return result;
}

static node_result_t wait_for_ack(const node_context_t *ctx, const node_io_t *io)
{
    for (;;)
    {
        const int64_t now = io->now_us();
        if (now < ctx->tx_done_us)
        {
            return NODE_IO_ERROR;
        }
        if (now >= ctx->ack_deadline_us)
        {
            return NODE_TIMEOUT;
        }
        uint8_t frame[NODE_PACKET_MAX_SIZE];
        size_t len = 0;
        const node_result_t received = io->radio_receive(frame, sizeof(frame), &len,
                                                         ctx->ack_deadline_us);
        if (received == NODE_BUFFER_TOO_SMALL)
        {
            io->trace("RX frame discarded", received, NULL, 0);
            continue;
        }
        if (received != NODE_OK)
        {
            return received;
        }
        node_packet_t packet;
        const node_result_t decoded = node_packet_decode(frame, len, &packet);
        /* Đọc clock sau decode, thời gian xử lý cũng nằm trong cửa sổ ACK. */
        const int64_t accepted_us = io->now_us();
        if (decoded == NODE_OK && node_state_accept_ack(ctx, &packet, accepted_us))
        {
            io->trace("ACK accepted", NODE_OK, frame, len);
            return NODE_OK;
        }
        io->trace("RX frame ignored", decoded == NODE_OK ? NODE_INVALID_FRAME : decoded,
                  frame, len <= sizeof(frame) ? len : 0);
    }
}

node_result_t node_app_init(node_context_t *ctx, const node_config_t *config,
                            const node_io_t *io)
{
    if (ctx != NULL)
    {
        ctx->app_initialized = false;
    }
    if (!valid_arguments(ctx, config, io))
    {
        return NODE_INVALID_ARG;
    }
    node_state_init(ctx, config);
    const node_result_t result = io->radio_init();
    if (result != NODE_OK)
    {
        io->trace("Radio init failed", result, NULL, 0);
        return result;
    }
    ctx->sensor_init_result = io->sensor_init();
    if (ctx->sensor_init_result != NODE_OK)
    {
        io->trace("Sensor init failed", ctx->sensor_init_result, NULL, 0);
    }
    ctx->battery_init_result = io->battery_init();
    if (ctx->battery_init_result != NODE_OK)
    {
        io->trace("Battery init failed", ctx->battery_init_result, NULL, 0);
    }
    ctx->app_initialized = true;
    return NODE_OK;
}

node_result_t node_app_run_cycle(node_context_t *ctx, const node_config_t *config,
                                 const node_io_t *io)
{
    if (!valid_arguments(ctx, config, io))
    {
        return NODE_INVALID_ARG;
    }
    if (!ctx->app_initialized)
    {
        return NODE_INIT_FAILED;
    }
    if (ctx->state != NODE_STATE_SLEEP || ctx->node_addr != config->node_addr ||
        ctx->gateway_addr != config->gateway_addr || ctx->ack_timeout_ms != config->ack_timeout_ms)
    {
        return NODE_INVALID_ARG;
    }

    node_result_t result = io->radio_sleep();
    if (result == NODE_OK)
    {
        result = io->sleep_ms(config->sleep_ms);
    }
    if (result != NODE_OK)
    {
        io->trace("Sleep preparation failed", result, NULL, 0);
        return result;
    }

    ctx->state = NODE_STATE_MEASURE;
    uint8_t frame[NODE_PACKET_MAX_SIZE];
    size_t len = 0;
    result = prepare_frame(ctx, io, frame, &len);
    if (result == NODE_OK)
    {
        ctx->state = NODE_STATE_TRANSMIT;
        io->trace("TX frame", NODE_OK, frame, len);
        int64_t tx_done_us = 0;
        result = io->radio_send(frame, len, config->tx_timeout_ms, &tx_done_us);
        if (result == NODE_OK)
        {
            node_state_begin_ack_wait(ctx, tx_done_us);
            result = ctx->state == NODE_STATE_WAIT_ACK ? wait_for_ack(ctx, io) : NODE_IO_ERROR;
        }
    }

    /* Một đường kết thúc cho mọi lượt đã bắt đầu, không retry hoặc giữ mẫu cũ. */
    const node_result_t sleep_result = io->radio_sleep();
    if (sleep_result != NODE_OK)
    {
        io->trace("Radio sleep failed", sleep_result, NULL, 0);
        if (result == NODE_OK)
        {
            result = sleep_result;
        }
    }
    node_state_finish_cycle(ctx);
    io->trace("Cycle finished", result, NULL, 0);
    return result;
}
