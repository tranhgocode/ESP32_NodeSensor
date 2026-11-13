#ifndef SENSOR_HMI3301_H
#define SENSOR_HMI3301_H

#include <stdbool.h>
#include <stdint.h>
#include "node_types.h"

/* Chỉ luồng ứng dụng sử dụng driver, tài nguyên được giữ qua Light-sleep.
 * App khởi tạo một lần, driver không tự reset hoặc khởi tạo lại khi lỗi. */

/* Chuẩn bị SET LOW, chỉ mượn bus I2C khi enabled là true.
 * Không gửi lệnh đến cảm biến đang ngủ. Nếu caller chủ động gọi init lại,
 * driver gỡ device và trả lượt giữ bus cũ trước khi tạo device mới.
 * Cleanup lỗi giữ lại handle để không xóa bus mà cảm biến khác đang dùng. */
node_result_t hm3301_init(bool enabled);

/* Đưa SET HIGH và trả mốc sẵn sàng theo clock monotonic, đơn vị micro giây.
 * warmup_ms phải ít nhất 30000, ready_at_us chỉ đổi khi thành công.
 * Caller chịu trách nhiệm chờ và đưa cảm biến về Sleep nếu wake báo lỗi. */
node_result_t hm3301_wake(uint32_t warmup_ms, int64_t *ready_at_us);

/* Đọc một mẫu PM2.5 Atmospheric, đơn vị ug/m3, sau khi đủ warmup.
 * Kiểm tra checksum trước khi ghi output, không retry hoặc trả mẫu cũ.
 * NODE_NOT_READY báo chưa đủ warmup, mọi lỗi giữ nguyên out_ug_m3. */
node_result_t hm3301_read_pm25(uint16_t *out_ug_m3);

/* Đưa SET LOW và vô hiệu hóa trạng thái sẵn sàng đọc, không cần giao tiếp I2C.
 * Dùng được sau init dở dang nếu SET đã cấu hình thành công. */
node_result_t hm3301_sleep(void);

#endif
