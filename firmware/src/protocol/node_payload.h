#ifndef PROTOCOL_NODE_PAYLOAD_H
#define PROTOCOL_NODE_PAYLOAD_H

#include <stdint.h>

#include "node_types.h"
#include "protocol/node_packet.h"

#define NODE_BATTERY_MV_UNAVAILABLE UINT16_MAX

/**
 * Mã hóa mẫu đo và điện áp pin thành payload DATA cố định 6 byte.
 *
 * Hai số x10 được ghi theo byte cao trước: nhiệt độ có dấu ở byte 0..1 dưới
 * dạng 16 bit, độ ẩm không dấu ở byte 2..3, điện áp pin mV ở byte 4..5.
 * UINT16_MAX biểu thị điện áp không có dữ liệu. Đây chỉ là chuyển đổi định dạng,
 * hàm không kiểm tra miền vật lý của phép đo.
 *
 * @param sample Mẫu nguồn có hai trường x10 hợp lệ trong kiểu của chúng.
 * @param battery_mv Điện áp pin trước chia áp, hoặc NODE_BATTERY_MV_UNAVAILABLE.
 * @param out Bộ đệm đích do bên gọi sở hữu, cần ít nhất 6 byte ghi được.
 * @return NODE_OK, NODE_INVALID_ARG nếu sample hoặc out là NULL. Khi lỗi,
 *         out không bị thay đổi.
 * @pre Nếu sample khác NULL, sample phải trỏ tới một node_measurement_t đọc
 *      được, nếu out khác NULL, out phải có 6 byte ghi được.
 */
node_result_t node_payload_encode_data(const node_measurement_t *sample,
                                       uint16_t battery_mv,
                                       uint8_t out[NODE_DATA_PAYLOAD_SIZE]);

/**
 * Mã hóa mã lỗi thành payload ERROR một byte.
 *
 * @param error_code Mã giao thức trong khoảng 1..4.
 * @param out Bộ đệm đích do bên gọi sở hữu, cần ít nhất 1 byte ghi được.
 * @return NODE_OK, NODE_INVALID_ARG nếu out là NULL, NODE_OUT_OF_RANGE nếu mã
 *         nằm ngoài 1..4. Khi lỗi, out không bị thay đổi.
 * @pre Nếu out khác NULL, out phải có ít nhất một byte ghi được.
 */
node_result_t node_payload_encode_error(uint8_t error_code, uint8_t out[1]);

#endif
