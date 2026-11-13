#ifndef PLATFORM_NODE_I2C_H
#define PLATFORM_NODE_I2C_H

#include "driver/i2c_master.h"
#include "node_types.h"

/* Chỉ luồng ứng dụng gọi tuần tự, không dùng từ ISR.
 * Mỗi acquire thành công giữ một lượt sử dụng bus, output không đổi khi lỗi.
 * Caller phải gỡ device của mình trước release để không xóa bus còn cần dùng.
 * Lỗi release giữ nguyên lượt sử dụng và handle để caller còn quyền cleanup.
 * NODE_INVALID_ARG báo NULL output hoặc release không có lượt sử dụng,
 * NODE_OUT_OF_RANGE báo bộ đếm đầy, lỗi SDK được ánh xạ sang TIMEOUT hoặc IO_ERROR.
 */
node_result_t node_i2c_acquire(i2c_master_bus_handle_t *out_bus);
node_result_t node_i2c_release(void);

#endif
