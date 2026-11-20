#include "protocol/node_packet.h"

#include "protocol/crc16.h"

/** Kiểm tra định dạng theo loại gói. DATA chỉ nhận 6, 8 hoặc 16 byte dữ liệu. */
static node_result_t validate_packet_shape(const node_packet_t *packet)
{
    switch (packet->type)
    {
        case NODE_PACKET_TYPE_POLL:
            if (packet->len != 0U || packet->seq != 0U)
            {
                return NODE_INVALID_FRAME;
            }
            break;

        case NODE_PACKET_TYPE_DATA:
            if (packet->len != NODE_DATA_PAYLOAD_SIZE &&
                packet->len != NODE_DATA_PM25_PAYLOAD_SIZE &&
                packet->len != NODE_DATA_FORECAST_PAYLOAD_SIZE)
            {
                return NODE_INVALID_FRAME;
            }
            break;

        case NODE_PACKET_TYPE_ACK:
            if (packet->len != 0U)
            {
                return NODE_INVALID_FRAME;
            }
            break;

        case NODE_PACKET_TYPE_ERROR:
            /* Kiểm tra độ dài trước khi đọc mã ERROR. */
            if (packet->len != 1U || packet->payload[0] < 1U || packet->payload[0] > 4U)
            {
                return NODE_INVALID_FRAME;
            }
            break;

        default:
            return NODE_INVALID_FRAME;
    }

    return NODE_OK;
}

node_result_t node_packet_encode(const node_packet_t *packet,
                                 uint8_t *out,
                                 size_t capacity,
                                 size_t *written)
{
    size_t frame_size;
    size_t index;
    uint16_t crc;

    if (written != NULL)
    {
        /* Độ dài bằng 0 nếu không tạo được khung. */
        *written = 0U;
    }

    if (packet == NULL || out == NULL || written == NULL)
    {
        return NODE_INVALID_ARG;
    }

    if (validate_packet_shape(packet) != NODE_OK)
    {
        return NODE_INVALID_FRAME;
    }

    frame_size = (size_t)packet->len + 9U;
    if (capacity < frame_size)
    {
        return NODE_BUFFER_TOO_SMALL;
    }

    out[0] = packet->type;
    out[1] = packet->src;
    out[2] = packet->dest;
    out[3] = packet->id;
    out[4] = packet->len;

    for (index = 0; index < packet->len; ++index)
    {
        out[5U + index] = packet->payload[index];
    }

    out[5U + packet->len] = (uint8_t)(packet->seq >> 8);
    out[6U + packet->len] = (uint8_t)packet->seq;

    /* CRC bao phủ từ Type đến byte thấp của Seq. */
    crc = crc16_ccitt_false(out, frame_size - 2U);
    out[7U + packet->len] = (uint8_t)(crc >> 8);
    out[8U + packet->len] = (uint8_t)crc;
    *written = frame_size;

    return NODE_OK;
}

node_result_t node_packet_decode(const uint8_t *frame,
                                 size_t len,
                                 node_packet_t *out)
{
    node_packet_t decoded = {0};
    size_t payload_len;
    size_t expected_size;
    size_t index;
    uint16_t received_crc;
    uint16_t calculated_crc;

    if (frame == NULL || out == NULL)
    {
        return NODE_INVALID_ARG;
    }

    if (len < 9U || len > NODE_PACKET_MAX_SIZE)
    {
        return NODE_INVALID_FRAME;
    }

    /* Chặn giá trị Len vượt sức chứa trước khi tính vị trí các trường. */
    payload_len = frame[4];
    if (payload_len > NODE_PACKET_MAX_PAYLOAD_SIZE)
    {
        return NODE_INVALID_FRAME;
    }

    expected_size = payload_len + 9U;
    if (len != expected_size)
    {
        return NODE_INVALID_FRAME;
    }

    decoded.type = frame[0];
    decoded.src = frame[1];
    decoded.dest = frame[2];
    decoded.id = frame[3];
    decoded.len = (uint8_t)payload_len;
    for (index = 0; index < payload_len; ++index)
    {
        decoded.payload[index] = frame[5U + index];
    }
    decoded.seq = (uint16_t)(((uint16_t)frame[5U + payload_len] << 8) |
                             frame[6U + payload_len]);

    if (validate_packet_shape(&decoded) != NODE_OK)
    {
        return NODE_INVALID_FRAME;
    }

    /* Ưu tiên báo lỗi định dạng trước lỗi CRC. */
    received_crc = (uint16_t)(((uint16_t)frame[len - 2U] << 8) | frame[len - 1U]);
    calculated_crc = crc16_ccitt_false(frame, len - 2U);
    if (received_crc != calculated_crc)
    {
        return NODE_CHECKSUM_ERROR;
    }

    *out = decoded;
    return NODE_OK;
}
