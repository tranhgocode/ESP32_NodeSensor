#ifndef FORECAST_PM25_HISTORY_H
#define FORECAST_PM25_HISTORY_H

#include <stdbool.h>

#define PM25_HISTORY_ROWS 24U
#define PM25_HISTORY_FEATURES 3U

typedef struct
{
    float values[PM25_HISTORY_ROWS][PM25_HISTORY_FEATURES];
} pm25_window_t;

typedef struct
{
    pm25_window_t window;
    unsigned rows_collected;
} pm25_history_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Ghi từng mẫu hợp lệ vào cửa sổ theo thứ tự đo.
 * @param history Bộ tích lũy, đặt về 0 trước lần gọi đầu.
 * @param values Các giá trị theo thứ tự PM2.5, nhiệt độ, độ ẩm.
 * @param valid True khi cả ba giá trị của cùng lần đo đều hợp lệ.
 * @return True khi đủ 24 hàng. Đọc history->window trước khi ghi mẫu tiếp theo.
 */
bool pm25_history_record(pm25_history_t *history,
                         const float values[PM25_HISTORY_FEATURES], bool valid);

#ifdef __cplusplus
}
#endif

#endif
