#ifndef PROTOCOL_NODE_PACKET_H
#define PROTOCOL_NODE_PACKET_H

#include <stddef.h>
#include <stdint.h>

#include "node_types.h"

/* Khung dây có 9 byte overhead ngoài payload; tổng tối đa là 64 byte. */
#define NODE_PACKET_MAX_SIZE 64U
#define NODE_PACKET_MAX_PAYLOAD_SIZE 55U

/* Mỗi loại có ràng buộc độ dài riêng; xem node_packet_encode/decode. */
#define NODE_PACKET_TYPE_POLL 0x01U
#define NODE_PACKET_TYPE_DATA 0x02U
#define NODE_PACKET_TYPE_ACK 0x03U
#define NODE_PACKET_TYPE_ERROR 0x04U

/**
 * Biểu diễn logic của gói tin. Đây không phải struct đóng gói theo wire:
 * payload có vùng chứa cố định 55 byte và seq nằm cuối struct trong RAM,
 * nhưng trên khung dây seq được đặt ngay sau đúng len byte payload.
 */
typedef struct
{
    uint8_t type;   /**< Loại gói: POLL, DATA, ACK hoặc ERROR. */
    uint8_t src;    /**< Địa chỉ thiết bị gửi; với ACK, đây có thể là gateway. */
    uint8_t dest;   /**< Địa chỉ thiết bị nhận. */
    uint8_t id;     /**< Mã định danh gói. */
    uint8_t len;    /**< Số byte payload có nghĩa, không phải kích thước mảng. */
    uint8_t payload[NODE_PACKET_MAX_PAYLOAD_SIZE]; /**< Chỉ len byte đầu được dùng. */
    uint16_t seq;   /**< Số thứ tự 16 bit; lên dây theo byte cao rồi byte thấp. */
} node_packet_t;

/**
 * Tuần tự hóa gói logic sang khung dây.
 *
 * Bố cục byte là type, src, dest, id, len, payload[0..len-1], seq_hi,
 * seq_lo, crc_hi, crc_lo. Vì vậy seq ở offset 5+len, CRC ở 7+len, và tổng
 * kích thước là len+9; seq và CRC đều truyền byte cao trước. CRC bao phủ mọi
 * byte trước hai byte CRC. Cấu trúc RAM node_packet_t không được chép nguyên khối.
 *
 * @param packet Gói nguồn; hàm chỉ đọc len byte payload.
 * @param out Bộ đệm đích do bên gọi sở hữu và cấp phát.
 * @param capacity Dung lượng out theo byte; phải đủ len+9 byte.
 * @param written Con trỏ bắt buộc; được đặt về 0 nếu khác NULL trước khi kiểm
 *                tra đối số, và nhận kích thước khung khi thành công.
 * @return NODE_OK; NODE_INVALID_ARG nếu thiếu con trỏ; NODE_INVALID_FRAME nếu
 *         loại/độ dài/seq hoặc mã ERROR sai; NODE_BUFFER_TOO_SMALL nếu out
 *         không đủ chỗ. Hàm không ghi byte khung nào qua out trước khi các
 *         kiểm tra đối số, hình dạng và sức chứa thành công; riêng written
 *         được đặt về 0 nếu khác NULL trước các kiểm tra.
 * @pre Nếu packet khác NULL, struct phải đọc được; với gói có hình dạng hợp lệ,
 *      các byte payload được len chỉ ra phải đọc được. Nếu out khác NULL, vùng
 *      đích phải ghi được ít nhất capacity byte; written nếu khác NULL phải ghi được.
 */
node_result_t node_packet_encode(const node_packet_t *packet,
                                 uint8_t *out,
                                 size_t capacity,
                                 size_t *written);

/**
 * Kiểm tra và giải mã một khung dây thành gói logic.
 *
 * Khung cần dài 9..64 byte, có độ dài chính xác len_field+9, payload không
 * quá 55 byte, loại và hình dạng hợp lệ, rồi mới được chấp nhận khi CRC đúng.
 * Các trường 16 bit được đọc theo thứ tự byte cao trước. Hàm dựng kết quả cục
 * bộ và chỉ chép sang out sau khi mọi kiểm tra thành công, nên out không đổi
 * nếu có lỗi.
 *
 * @param frame Bộ đệm khung do bên gọi sở hữu.
 * @param len Số byte hợp lệ có thể đọc trong frame.
 * @param out Struct đích do bên gọi sở hữu; chỉ được cập nhật khi thành công.
 * @return NODE_OK; NODE_INVALID_ARG nếu frame hoặc out là NULL;
 *         NODE_INVALID_FRAME nếu giới hạn/kích thước/hình dạng sai;
 *         NODE_CHECKSUM_ERROR nếu khung đúng hình dạng nhưng CRC không khớp.
 * @pre Nếu frame khác NULL, frame phải trỏ tới ít nhất len byte đọc được; nếu
 *      out khác NULL, out phải ghi được một node_packet_t.
 */
node_result_t node_packet_decode(const uint8_t *frame,
                                 size_t len,
                                 node_packet_t *out);

#endif
