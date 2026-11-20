#ifndef PROTOCOL_NODE_PACKET_H
#define PROTOCOL_NODE_PACKET_H

#include <stddef.h>
#include <stdint.h>

#include "node_types.h"

/* Khung tối đa 64 byte, gồm 9 byte cố định và phần dữ liệu. */
#define NODE_PACKET_MAX_SIZE 64U
#define NODE_PACKET_MAX_PAYLOAD_SIZE 55U

/* Phần dữ liệu của DATA dài 6 byte, 8 byte nếu kèm PM2.5 hoặc 16 byte nếu kèm dự báo. */
#define NODE_PACKET_TYPE_POLL 0x01U
#define NODE_PACKET_TYPE_DATA 0x02U
#define NODE_DATA_PAYLOAD_SIZE 6U
#define NODE_DATA_PM25_PAYLOAD_SIZE 8U
#define NODE_PM25_FORECAST_COUNT 4U
#define NODE_DATA_FORECAST_PAYLOAD_SIZE (NODE_DATA_PM25_PAYLOAD_SIZE + 2U * NODE_PM25_FORECAST_COUNT)
#define NODE_PACKET_TYPE_ACK 0x03U
#define NODE_PACKET_TYPE_ERROR 0x04U

/** Gói tin trong RAM. Khi truyền, Seq nằm ngay sau len byte dữ liệu. */
typedef struct
{
    uint8_t type;   /**< Loại gói POLL, DATA, ACK hoặc ERROR. */
    uint8_t src;    /**< Địa chỉ nguồn. */
    uint8_t dest;   /**< Địa chỉ đích. */
    uint8_t id;     /**< ID gói. */
    uint8_t len;    /**< Số byte dữ liệu có hiệu lực. */
    uint8_t payload[NODE_PACKET_MAX_PAYLOAD_SIZE];
    uint16_t seq;   /**< Số thứ tự 16 bit, truyền byte cao trước. */
} node_packet_t;

/**
 * Mã hóa khung Type | Src | Dest | ID | Len | Payload | Seq | CRC.
 * Seq và CRC đặt byte cao trước. CRC tính từ Type đến hết Seq.
 * @param packet Gói nguồn.
 * @param out Bộ đệm đích do bên gọi cấp.
 * @param capacity Dung lượng out, cần ít nhất Len + 9 byte.
 * @param written Số byte đã ghi. Đặt về 0 khi lỗi nếu khác NULL.
 * @return NODE_OK, NODE_INVALID_ARG, NODE_INVALID_FRAME hoặc NODE_BUFFER_TOO_SMALL.
 *         Lỗi không sửa out.
 */
node_result_t node_packet_encode(const node_packet_t *packet,
                                 uint8_t *out,
                                 size_t capacity,
                                 size_t *written);

/**
 * Kiểm tra độ dài, định dạng và CRC rồi giải mã khung.
 * @param frame Khung nguồn có ít nhất len byte đọc được.
 * @param len Số byte của khung, bằng Len + 9 và tối đa 64.
 * @param out Gói đích, chỉ ghi khi thành công.
 * @return NODE_OK, NODE_INVALID_ARG nếu thiếu con trỏ, NODE_INVALID_FRAME
 *         nếu sai định dạng hoặc NODE_CHECKSUM_ERROR nếu CRC không khớp.
 */
node_result_t node_packet_decode(const uint8_t *frame,
                                 size_t len,
                                 node_packet_t *out);

#endif
