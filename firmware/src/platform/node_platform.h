#ifndef PLATFORM_NODE_PLATFORM_H
#define PLATFORM_NODE_PLATFORM_H

#include "app/node_io.h"
#include "node_config.h"

/** Bảng callback const, chỉ một task ứng dụng sử dụng và sở hữu driver. */
const node_io_t *node_platform_io(void);

/** In cấu hình trước lượt đầu, không gọi trong cửa sổ TX/ACK. */
void node_platform_log_config(const node_config_t *config);

/**
 * In và xóa trace đã copy, chỉ gọi sau init hoặc sau run_cycle trả về.
 * Bộ đệm hữu hạn, trace vượt giới hạn bị bỏ và báo số lượng khi flush.
 * Trace không in UART, không giữ con trỏ buffer của callback.
 */
void node_platform_flush_trace(void);

#endif
