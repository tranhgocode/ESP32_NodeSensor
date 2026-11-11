#ifndef NODE_DEBUG_H
#define NODE_DEBUG_H

/* Đặt 1 để bật log ứng dụng, đặt 0 để tắt log ứng dụng.
 * Có thể ghi đè bằng build flag -DIS_DEBUG_TRACE_LOG=0.
 * Khi chạy thực tế hãy set 0 để tiếp kiệm năng lượng. */
#ifndef IS_DEBUG_TRACE_LOG
#define IS_DEBUG_TRACE_LOG 1
#endif

#if IS_DEBUG_TRACE_LOG != 0 && IS_DEBUG_TRACE_LOG != 1
#error "IS_DEBUG_TRACE_LOG must be 0 or 1"
#endif

#include "esp_log.h"

/** Gọi đầu app_main, trước mọi log của ứng dụng và driver. */
static inline void debug_trace_log_init(void)
{
#if IS_DEBUG_TRACE_LOG
    esp_log_level_set("*", ESP_LOG_INFO);
#else
    esp_log_level_set("*", ESP_LOG_NONE);
#endif
}

#endif
