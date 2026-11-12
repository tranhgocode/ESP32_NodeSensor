#ifndef SENSOR_BATTERY_H
#define SENSOR_BATTERY_H

#include <stdint.h>
#include "node_types.h"

/* Một task ứng dụng sở hữu driver và các handle ADC/calibration private.
 * Init được gọi một lần lúc khởi động, gọi lại tường minh sẽ thu hồi tài
 * nguyên cũ trước khi tạo mới. Không tự reinit khi đọc lỗi. */
node_result_t battery_init(void);

/* Đọc một bộ mẫu đã hiệu chuẩn và hoàn nguyên tỷ lệ chia áp, trả mV của pin.
 * Chỉ ghi out_mv khi thành công, không trả UINT16_MAX hoặc dùng mẫu cũ.
 * ADC bão hòa, calibration không có hoặc phép đọc lỗi đều được báo về app. */
node_result_t battery_read_mv(uint16_t *out_mv);

#endif
