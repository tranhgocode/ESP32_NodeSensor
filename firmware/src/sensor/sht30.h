#ifndef SENSOR_SHT30_H
#define SENSOR_SHT30_H

#include "node_types.h"

/* Chỉ luồng ứng dụng sở hữu driver, không cấp nguồn hoặc tự dò địa chỉ.
 * Init tắt periodic/heater và xác nhận status để tránh đo với trạng thái cũ.
 * NODE_INIT_FAILED báo init hoặc cleanup lỗi, handle còn sở hữu được giữ lại.
 * Caller chủ động gọi init lại sẽ cleanup trước, read không bao giờ tự init.
 */
node_result_t sht30_init(void);

/* Một lệnh single-shot và một frame, caller sở hữu out, chỉ đổi khi NODE_OK.
 * Trả NODE_INVALID_ARG nếu out NULL, NODE_INIT_FAILED nếu chưa sẵn sàng,
 * NODE_TIMEOUT/NODE_IO_ERROR cho lỗi giao tiếp hoặc clock,
 * NODE_CHECKSUM_ERROR cho CRC sai, NODE_OUT_OF_RANGE cho dữ liệu hoặc clock tràn.
 * Kết quả x10 chấp nhận -40..125°C và 0..100%RH, không retry hay giữ mẫu cũ.
 */
node_result_t sht30_read(node_measurement_t *out);

#endif
