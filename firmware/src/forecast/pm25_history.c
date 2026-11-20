#include "forecast/pm25_history.h"

#include <stdbool.h>

#define PM25_MAX_MISSING_ROWS 3U

/**
 * Tính trung bình từng hàng cho một biến đầu vào.
 * Chỉ nội suy tuyến tính tối đa ba hàng thiếu liên tiếp có dữ liệu ở hai đầu.
 */
static bool complete_feature(const pm25_history_t *history, unsigned feature,
                             pm25_window_t *window)
{
    unsigned row = 0;
    while (row < PM25_HISTORY_ROWS)
    {
        const uint32_t count = history->counts[row][feature];
        if (count != 0)
        {
            window->values[row][feature] = history->sums.values[row][feature] / (float)count;
            ++row;
            continue;
        }

        const unsigned first_missing = row;
        while (row < PM25_HISTORY_ROWS && history->counts[row][feature] == 0)
        {
            ++row;
        }
        const unsigned gap = row - first_missing;
        if (first_missing == 0 || row == PM25_HISTORY_ROWS || gap > PM25_MAX_MISSING_ROWS)
        {
            return false;
        }

        const float left = window->values[first_missing - 1][feature];
        const float right = history->sums.values[row][feature] / (float)history->counts[row][feature];
        for (unsigned offset = 0; offset < gap; ++offset)
        {
            const float fraction = (float)(offset + 1) / (float)(gap + 1);
            window->values[first_missing + offset][feature] = left + (right - left) * fraction;
        }
    }
    return true;
}

pm25_history_result_t pm25_history_record(pm25_history_t *history, int64_t due_us,
                                         int64_t interval_us, int64_t row_us,
                                         const float values[PM25_HISTORY_FEATURES],
                                         uint8_t valid, pm25_window_t *window)
{
    if (due_us <= 0 || interval_us <= 0 || row_us < interval_us)
    {
        return PM25_HISTORY_INCOMPLETE;
    }

    /* Mẫu đúng biên được tính vào hàng vừa kết thúc. */
    const int64_t absolute_row = (due_us - 1) / row_us;
    const int64_t window_index = absolute_row / PM25_HISTORY_ROWS;
    const unsigned row = (unsigned)(absolute_row % PM25_HISTORY_ROWS);
    if (window_index != history->window_index)
    {
        *history = (pm25_history_t){.window_index = window_index};
    }

    for (unsigned feature = 0; feature < PM25_HISTORY_FEATURES; ++feature)
    {
        if ((valid & (1U << feature)) != 0)
        {
            history->sums.values[row][feature] += values[feature];
            ++history->counts[row][feature];
        }
    }

    const int64_t remaining_in_row_us = row_us - 1 - (due_us - 1) % row_us;
    if (row != PM25_HISTORY_ROWS - 1 || remaining_in_row_us >= interval_us)
    {
        return PM25_HISTORY_COLLECTING;
    }

    pm25_history_result_t result = PM25_HISTORY_READY;
    for (unsigned feature = 0; feature < PM25_HISTORY_FEATURES; ++feature)
    {
        if (!complete_feature(history, feature, window))
        {
            result = PM25_HISTORY_INCOMPLETE;
            break;
        }
    }
    return result;
}
