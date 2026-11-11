#ifndef RADIO_NODE_RADIO_H
#define RADIO_NODE_RADIO_H

#include <stddef.h>
#include <stdint.h>
#include "node_types.h"

/**
 * Khởi tạo Ra-02/SX1278 với cấu hình board/node đã chốt, kết thúc ở standby.
 * @return NODE_OK hoặc NODE_INIT_FAILED, dọn tài nguyên khởi tạo dở dang.
 * Gọi lại khi đã ready không init trùng. Adapter sở hữu SPI2 và handle riêng,
 * bus đã được thành phần khác sử dụng thì init thất bại, không free bus đó.
 * Tất cả API phải do cùng một task gọi tuần tự, ISR chỉ chuyển sự kiện.
 */
node_result_t node_radio_init(void);

/**
 * Gửi đúng một frame, không retry, đợi TX_DONE và chuyển RX trước khi trả OK.
 * @param frame Bộ đệm nguồn đọc được trong suốt lời gọi, không truyền struct C.
 * @param len Độ dài 1..64 byte.
 * @param timeout_ms Giới hạn TX, khác 0, caller node dùng 3.000 ms.
 * @param tx_done_us Con trỏ ghi được, không chồng frame, đặt 0 khi lỗi, nhận
 *                   timestamp monotonic của cạnh DIO0 TX_DONE khi thành công.
 * @return NODE_OK, NODE_INVALID_ARG, NODE_INIT_FAILED, NODE_TIMEOUT hoặc
 *         NODE_IO_ERROR. Lỗi TX không cấp cửa sổ ACK, không tự gửi lại frame.
 */
node_result_t node_radio_send(const uint8_t *frame, size_t len,
                              uint32_t timeout_ms, int64_t *tx_done_us);

/**
 * Nhận một frame vào bộ đệm caller trước deadline tuyệt đối (esp_timer clock).
 * @param out Bộ đệm ghi được capacity byte, không giữ con trỏ mượn từ radio.
 * @param capacity Dung lượng khác 0, frame hợp lệ lớn hơn capacity trả
 *                 NODE_BUFFER_TOO_SMALL và đã bị tiêu thụ.
 * @param len Con trỏ size_t ghi được, không chồng out, đặt 0 trước kiểm tra.
 * @param deadline_us Deadline gốc, không phải thời gian chờ mới cho mỗi gói.
 * @return NODE_OK, NODE_INVALID_ARG, NODE_INIT_FAILED, NODE_TIMEOUT,
 *         NODE_BUFFER_TOO_SMALL hoặc NODE_IO_ERROR. Trừ *len, output không
 *         đổi khi lỗi (các vùng caller không chồng nhau).
 * Hardware CRC lỗi/thiếu CRC, frame rỗng/quá 64 byte bị bỏ tới deadline cũ.
 * Không kiểm tra ACK matching hoặc application CRC, lớp protocol/state làm
 * việc đó. Có thể gọi lại cùng deadline khi nhận packet không liên quan.
 */
node_result_t node_radio_receive(uint8_t *out, size_t capacity, size_t *len,
                                 int64_t deadline_us);

/** Cho riêng radio vào Sleep, hủy IRQ cũ, không ngủ MCU. Lỗi trả IO_ERROR. */
node_result_t node_radio_sleep(void);

/**
 * Ngừng IRQ trước khi giải phóng queue/SPI. Gọi nhiều lần an toàn.
 * Lỗi cleanup được log, handle chưa giải phóng được giữ để deinit/init sau
 * thử lại. Không uninstall ISR service mượn hoặc free bus không sở hữu.
 */
void node_radio_deinit(void);

#endif
