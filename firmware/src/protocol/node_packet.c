#include "protocol/node_packet.h"

#include "protocol/crc16.h"

/* Ràng buộc này thuộc schema gói, tách khỏi bố cục byte và kiểm tra CRC. */
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
            if (packet->len != 4U)
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
            /* Kiểm tra len trước làm cho lần đọc payload[0] an toàn. */
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
        /* Tránh để caller hiểu nhầm là có một khung hoàn chỉnh khi gặp lỗi. */
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
    /* Kiểm tra sức chứa trước khi ghi bất kỳ byte khung nào qua out. */
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

    /* CRC tính từ type đến seq_lo; hai byte CRC không tự tham gia phép tính. */
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

    /* Giới hạn khung trước khi dùng len field để tính offset hoặc chép payload. */
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

    /* Chỉ đọc các offset seq và payload sau khi độ dài tổng đã khớp len field. */
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

    /* Giữ thứ tự ưu tiên hiện tại: sai schema trả INVALID_FRAME trước khi CRC. */
    received_crc = (uint16_t)(((uint16_t)frame[len - 2U] << 8) | frame[len - 1U]);
    calculated_crc = crc16_ccitt_false(frame, len - 2U);
    if (received_crc != calculated_crc)
    {
        return NODE_CHECKSUM_ERROR;
    }

    *out = decoded;
    return NODE_OK;
}
