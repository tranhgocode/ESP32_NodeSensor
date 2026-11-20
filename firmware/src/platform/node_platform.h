#ifndef PLATFORM_NODE_PLATFORM_H
#define PLATFORM_NODE_PLATFORM_H

#include "app/node_io.h"
#include "node_config.h"

/** Trả bảng hàm I/O tĩnh cho tác vụ ứng dụng. Hàm dự báo phụ thuộc cấu hình biên dịch. */
const node_io_t *node_platform_io(void);

/** In cấu hình và sơ đồ chân lúc khởi động. Ghi lỗi nếu config là NULL. */
void node_platform_log_config(const node_config_t *config);

/** In rồi xóa nhật ký đã lưu, kèm số sự kiện bị bỏ. Chỉ gọi sau khi khởi tạo hoặc sau một chu kỳ. */
void node_platform_flush_trace(void);

#endif
