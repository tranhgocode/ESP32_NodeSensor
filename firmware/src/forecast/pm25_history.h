#ifndef FORECAST_PM25_HISTORY_H
#define FORECAST_PM25_HISTORY_H

#include <stdint.h>

#define PM25_HISTORY_ROWS 24U
#define PM25_HISTORY_FEATURES 3U

typedef enum
{
    PM25_FEATURE_PM25 = 0,
    PM25_FEATURE_TEMPERATURE,
    PM25_FEATURE_HUMIDITY,
} pm25_feature_t;

typedef struct
{
    float values[PM25_HISTORY_ROWS][PM25_HISTORY_FEATURES];
} pm25_window_t;

typedef struct
{
    pm25_window_t sums;
    /* Ô có số mẫu bằng 0 là ô thiếu dữ liệu. Phép đo bằng 0 vẫn được đếm. */
    uint32_t counts[PM25_HISTORY_ROWS][PM25_HISTORY_FEATURES];
    int64_t window_index;
} pm25_history_t;

typedef enum
{
    PM25_HISTORY_COLLECTING,
    PM25_HISTORY_INCOMPLETE,
    PM25_HISTORY_READY,
} pm25_history_result_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Tích lũy mẫu theo cửa sổ 24 hàng. Chốt cửa sổ ở mốc đo cuối của hàng 24.
 * @param history Bộ tích lũy, đặt về 0 trước lần gọi đầu.
 * @param due_us Mốc đo tính từ cuối khởi tạo, dương và tăng dần.
 * @param interval_us Chu kỳ đo cố định, lớn hơn 0 và không dài hơn row_us.
 * @param row_us Độ dài cố định của một hàng, tính bằng µs.
 * @param values Các giá trị theo thứ tự PM2.5, nhiệt độ, độ ẩm.
 * @param valid Ba bit thấp đánh dấu các giá trị hợp lệ theo cùng thứ tự.
 * @param window Mảng kết quả 24 × 3. Chỉ đọc khi trả PM25_HISTORY_READY.
 * @return COLLECTING khi đang thu thập, READY khi đủ dữ liệu, INCOMPLETE
 *         khi thời gian không hợp lệ hoặc dữ liệu thiếu không thể nội suy.
 */
pm25_history_result_t pm25_history_record(pm25_history_t *history, int64_t due_us,
                                         int64_t interval_us, int64_t row_us,
                                         const float values[PM25_HISTORY_FEATURES],
                                         uint8_t valid, pm25_window_t *window);

#ifdef __cplusplus
}
#endif

#endif
