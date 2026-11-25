#ifndef FORECAST_PM25_HISTORY_H
#define FORECAST_PM25_HISTORY_H

#include <stdbool.h>
#include <stdint.h>

#define PM25_HISTORY_ROWS 24U
#define PM25_HISTORY_FEATURES 3U

typedef struct
{
    float values[PM25_HISTORY_ROWS][PM25_HISTORY_FEATURES];
} pm25_window_t;

typedef struct
{
    pm25_window_t observations;
    pm25_window_t window;
    uint64_t last_slot;
} pm25_history_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Ghi một mốc đo và chuẩn bị cửa sổ suy luận ở mốc 24, 28, 32 trở đi.
 * @param history Bộ tích lũy, đặt về 0 trước lần gọi đầu.
 * @param slot Chỉ số mốc đo định kỳ, bắt đầu từ 1 và tăng theo thứ tự thời gian.
 * @param values PM2.5, nhiệt độ, độ ẩm, dùng NaN cho từng giá trị thiếu.
 * @return True khi cửa sổ đủ dữ liệu sau nội suy. Chỉ đọc history->window khi trả true.
 */
bool pm25_history_record(pm25_history_t *history, uint64_t slot,
                         const float values[PM25_HISTORY_FEATURES]);

#ifdef __cplusplus
}
#endif

#endif
