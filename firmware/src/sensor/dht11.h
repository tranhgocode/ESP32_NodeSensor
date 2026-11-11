#ifndef SENSOR_DHT11_H
#define SENSOR_DHT11_H

#include "node_types.h"

/**
 * Chuẩn bị GPIO cho module DHT11 cấu hình trong board_config.h.
 *
 * @return NODE_OK nếu GPIO được cấu hình, NODE_INIT_FAILED nếu GPIO không
 *         phù hợp hoặc một thao tác cấu hình thất bại. Không dò sự hiện diện
 *         cảm biến: mất kết nối thường chỉ được phát hiện ở lần read timeout.
 * Module dùng open-drain và pull-up ngoài về 3.3V, không cấp nguồn cảm biến.
 * Caller phải chờ ít nhất 1 giây sau cấp nguồn, bài thử main chờ 5 giây.
 * Chỉ một task sở hữu driver, không gọi init/read đồng thời từ nhiều task.
 */
node_result_t dht11_init(void);

/**
 * Đọc một mẫu bằng API integer của esp-idf-lib/dht, không tự retry.
 *
 * @param out Bộ đệm mẫu do caller sở hữu, cập nhật chỉ khi NODE_OK.
 * @return NODE_OK, NODE_INVALID_ARG nếu out NULL, NODE_INIT_FAILED nếu chưa
 *         init, NODE_TIMEOUT/NODE_CHECKSUM_ERROR theo library, NODE_IO_ERROR
 *         cho lỗi đọc khác, NODE_OUT_OF_RANGE nếu mẫu ngoài miền được hỗ trợ.
 * Nhiệt độ/độ ẩm đã x10, không nhân lần nữa. Humidity âm bị loại trước khi
 * cast unsigned. Mọi lỗi giữ nguyên output, nội dung cũ không phải mẫu mới.
 *
 * Phiên bản library 1.2.0 giải mã DHT11 kiểu cũ: adapter chấp nhận 0..50°C,
 * 20..90%RH. Không hứa hỗ trợ encoding dấu/thập phân DHT11 đời mới chỉ bằng
 * đổi range. Caller giữ khoảng cách hơn 2 giây giữa các lần read và chỉ đọc
 * một lần mỗi lượt, hàm không tự delay, tạo packet hoặc quyết định gửi ERROR.
 */
node_result_t dht11_read(node_measurement_t *out);

#endif
