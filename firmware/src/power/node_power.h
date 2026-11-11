#ifndef POWER_NODE_POWER_H
#define POWER_NODE_POWER_H
#include <stdint.h>
#include "node_types.h"
/**
 * Ngủ Light-sleep tới hạn cố định dựa trên đồng hồ đơn điệu.
 * Nếu thức sớm, ngủ tiếp phần còn thiếu, không tính lại thời hạn từ đầu.
 * Bên gọi phải tắt LED và đưa radio vào Sleep trước khi gọi hàm này.
 * Chỉ một tác vụ được sử dụng API và bộ định thời đánh thức trong lúc ngủ.
 *
 * @param duration_ms Thời gian chờ tính bằng mili giây, 0 thì không làm gì.
 * @return NODE_OK khi đã tới hạn hoặc duration_ms bằng 0,
 *         NODE_OUT_OF_RANGE khi đồng hồ âm hoặc phép tính thời hạn bị tràn,
 *         NODE_IO_ERROR khi cấu hình/ngủ/dọn bộ định thời lỗi hoặc đồng hồ
 *         không tiến hay chạy lùi. Khi lỗi, bên gọi cần chờ trước khi thử
 *         lượt mới, không tiếp tục đo hoặc phát dữ liệu trong lượt đó.
 */
node_result_t node_power_sleep_ms(uint32_t duration_ms);
#endif
