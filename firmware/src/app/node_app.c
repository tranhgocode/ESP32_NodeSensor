#include "app/node_app.h"
#include "protocol/node_payload.h"

#define NODE_ERROR_SENSOR_INIT 0x01U
#define NODE_ERROR_SENSOR_READ 0x02U
#define NODE_ERROR_SENSOR_RANGE 0x03U
#define NODE_ERROR_DATA_ENCODING 0x04U
#define NODE_FRAME_OVERHEAD 9U
#define MICROSECONDS_PER_MILLISECOND INT64_C(1000)

static bool hm3301_available(const node_context_t *ctx)
{
    return ctx->hm3301_enabled && ctx->hm3301_init_result == NODE_OK;
}

static void sleep_hm3301(const node_context_t *ctx, const node_io_t *io)
{
    if (ctx->hm3301_enabled)
    {
        const node_result_t result = io->hm3301_sleep();
        if (result != NODE_OK)
        {
            io->trace("HM3301 sleep failed", result, NULL, 0);
        }
    }
}

static node_result_t select_measurement_due(const node_context_t *ctx,
                                            const node_config_t *config,
                                            const node_io_t *io, int64_t *due_us)
{
    const int64_t now = io->now_us();
    const int64_t previous = ctx->last_measurement_due_us;
    if (now < 0 || previous < 0 || now < previous)
    {
        return NODE_IO_ERROR;
    }
    const int64_t interval = (int64_t)config->measurement_interval_ms * MICROSECONDS_PER_MILLISECOND;
    const int64_t preparation = hm3301_available(ctx) ?
        (int64_t)config->hm3301_warmup_ms * MICROSECONDS_PER_MILLISECOND : 0;
    if (now > INT64_MAX - preparation)
    {
        return NODE_OUT_OF_RANGE;
    }
    /* Chọn mốc còn đủ warmup trên lịch cũ, không phát bù các mốc đã lỡ. */
    const int64_t remaining = now + preparation - previous;
    int64_t slots = remaining / interval + (remaining % interval != 0);
    if (slots == 0)
    {
        slots = 1;
    }
    if (slots > (INT64_MAX - previous) / interval)
    {
        return NODE_OUT_OF_RANGE;
    }
    *due_us = previous + slots * interval;
    return NODE_OK;
}

static node_result_t sleep_until(const node_io_t *io, int64_t deadline_us)
{
    int64_t now = io->now_us();
    if (now < 0 || deadline_us < 0)
    {
        return NODE_IO_ERROR;
    }
    while (now < deadline_us)
    {
        const int64_t remaining = deadline_us - now;
        /* Làm tròn lên để không rút ngắn thời gian chờ còn lại dưới một ms. */
        const int64_t rounded_ms = remaining / MICROSECONDS_PER_MILLISECOND +
                                   (remaining % MICROSECONDS_PER_MILLISECOND != 0);
        const uint32_t sleep_ms = rounded_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)rounded_ms;
        const node_result_t result = io->sleep_ms(sleep_ms);
        if (result != NODE_OK)
        {
            return result;
        }
        const int64_t after = io->now_us();
        if (after <= now)
        {
            return NODE_IO_ERROR;
        }
        now = after;
    }
    return NODE_OK;
}

static node_result_t wait_for_measurement(const node_context_t *ctx,
                                          const node_config_t *config,
                                          const node_io_t *io, int64_t due_us,
                                          bool *hm_ready)
{
    *hm_ready = false;
    sleep_hm3301(ctx, io);
    if (!hm3301_available(ctx))
    {
        return sleep_until(io, due_us);
    }
    const int64_t warmup_us = (int64_t)config->hm3301_warmup_ms * MICROSECONDS_PER_MILLISECOND;
    node_result_t result = sleep_until(io, due_us - warmup_us);
    if (result != NODE_OK)
    {
        return result;
    }
    const int64_t wake_started = io->now_us();
    if (wake_started < 0 || wake_started > INT64_MAX - warmup_us)
    {
        return NODE_IO_ERROR;
    }
    int64_t ready_at_us = 0;
    result = io->hm3301_wake(config->hm3301_warmup_ms, &ready_at_us);
    if (result != NODE_OK)
    {
        io->trace("HM3301 wake failed", result, NULL, 0);
        sleep_hm3301(ctx, io);
        return sleep_until(io, due_us);
    }
    if (ready_at_us < wake_started + warmup_us)
    {
        return NODE_IO_ERROR;
    }
    /* SET có thể lên HIGH trễ, luôn ưu tiên đủ warmup thay vì đọc sớm cho kịp mốc. */
    result = sleep_until(io, ready_at_us > due_us ? ready_at_us : due_us);
    *hm_ready = result == NODE_OK;
    return result;
}

static bool valid_arguments(const node_context_t *ctx, const node_config_t *config,
                             const node_io_t *io)
{
    return ctx != NULL && config != NULL && io != NULL &&
           config->node_addr != 0 && config->node_addr != UINT8_MAX &&
           config->gateway_addr != UINT8_MAX && config->node_addr != config->gateway_addr &&
           config->measurement_interval_ms != 0 && config->tx_timeout_ms != 0 && config->ack_timeout_ms != 0 &&
           (!config->hm3301_enabled ||
            (config->hm3301_warmup_ms >= NODE_HM3301_MIN_WARMUP_MS &&
             config->measurement_interval_ms > config->hm3301_warmup_ms)) &&
           io->sensor_init != NULL && io->radio_init != NULL && io->sensor_read != NULL &&
           io->battery_init != NULL && io->battery_read_mv != NULL &&
           io->hm3301_init != NULL && io->hm3301_wake != NULL &&
           io->hm3301_read_pm25 != NULL && io->hm3301_sleep != NULL &&
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
                                    bool hm_ready, uint8_t *frame, size_t *len)
{
    node_packet_t packet = {
        .type = NODE_PACKET_TYPE_DATA,
        .src = ctx->node_addr,
        .dest = ctx->gateway_addr,
        .id = ctx->id,
        .seq = ctx->seq,
        .len = ctx->hm3301_enabled ? NODE_DATA_PM25_PAYLOAD_SIZE : NODE_DATA_PAYLOAD_SIZE,
    };
    const uint16_t battery_mv = read_battery_mv(ctx, io);
    bool hm_slept = false;
    uint8_t error = NODE_ERROR_SENSOR_INIT;
    if (ctx->sensor_init_result == NODE_OK)
    {
        node_measurement_t sample = {0};
        const node_result_t read_result = io->sensor_read(&sample);
        if (read_result == NODE_OK)
        {
            uint16_t pm25 = NODE_PM25_UNAVAILABLE;
            if (hm_ready)
            {
                uint16_t fresh_pm25 = 0;
                const node_result_t pm_result = io->hm3301_read_pm25(&fresh_pm25);
                if (pm_result == NODE_OK)
                {
                    pm25 = fresh_pm25;
                }
                else
                {
                    io->trace("HM3301 read failed", pm_result, NULL, 0);
                }
            }
            sleep_hm3301(ctx, io);
            hm_slept = true;
            node_result_t result = ctx->hm3301_enabled ?
                node_payload_encode_data_pm25(&sample, battery_mv, pm25, packet.payload) :
                node_payload_encode_data(&sample, battery_mv, packet.payload);
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

    if (!hm_slept)
    {
        sleep_hm3301(ctx, io);
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
    ctx->hm3301_init_result = io->hm3301_init(config->hm3301_enabled);
    if (ctx->hm3301_init_result != NODE_OK)
    {
        io->trace("HM3301 init failed", ctx->hm3301_init_result, NULL, 0);
    }
    const int64_t now = io->now_us();
    if (now < 0)
    {
        return NODE_IO_ERROR;
    }
    ctx->last_measurement_due_us = now;
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
        ctx->gateway_addr != config->gateway_addr || ctx->ack_timeout_ms != config->ack_timeout_ms ||
        ctx->tx_timeout_ms != config->tx_timeout_ms ||
        ctx->measurement_interval_ms != config->measurement_interval_ms ||
        ctx->hm3301_enabled != config->hm3301_enabled ||
        ctx->hm3301_warmup_ms != config->hm3301_warmup_ms)
    {
        return NODE_INVALID_ARG;
    }

    int64_t due_us = 0;
    bool hm_ready = false;
    node_result_t result = select_measurement_due(ctx, config, io, &due_us);
    if (result == NODE_OK)
    {
        result = io->radio_sleep();
    }
    if (result == NODE_OK)
    {
        result = wait_for_measurement(ctx, config, io, due_us, &hm_ready);
    }
    if (result != NODE_OK)
    {
        sleep_hm3301(ctx, io);
        io->trace("Sleep preparation failed", result, NULL, 0);
        return result;
    }

    ctx->state = NODE_STATE_MEASURE;
    ctx->last_measurement_due_us = due_us;
    uint8_t frame[NODE_PACKET_MAX_SIZE];
    size_t len = 0;
    result = prepare_frame(ctx, io, hm_ready, frame, &len);
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
