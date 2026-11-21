#include "forecast/pm25_history.h"

#include <string.h>

bool pm25_history_record(pm25_history_t *history,
                         const float values[PM25_HISTORY_FEATURES], bool valid)
{
    if (!valid)
    {
        return false;
    }

    memcpy(history->window.values[history->rows_collected], values,
           sizeof(history->window.values[0]));
    ++history->rows_collected;
    if (history->rows_collected < PM25_HISTORY_ROWS)
    {
        return false;
    }

    /* Giữ cửa sổ hoàn chỉnh cho lần suy luận hiện tại. Mẫu tiếp theo mở nhóm mới. */
    history->rows_collected = 0;
    return true;
}
