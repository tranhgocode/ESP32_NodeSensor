#ifndef PROTOCOL_CRC16_H
#define PROTOCOL_CRC16_H

#include <stddef.h>
#include <stdint.h>

/**
 * Tính CRC-16/CCITT-FALSE cho dãy byte đầu vào.
 *
 * @param data Dãy len byte có thể đọc, nếu NULL, hàm trả về giá trị khởi tạo
 *             0xFFFF mà không đọc dữ liệu.
 * @param len Số byte cần tính. len bằng 0 với data khác NULL cũng cho CRC
 *            khởi tạo 0xFFFF.
 * @return Giá trị CRC 16 bit, API này không có mã lỗi riêng.
 * @pre Khi len > 0 và data khác NULL, data phải trỏ tới ít nhất len byte đọc được.
 */
uint16_t crc16_ccitt_false(const uint8_t *data, size_t len);

#endif
