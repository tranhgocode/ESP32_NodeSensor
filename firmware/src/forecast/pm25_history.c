#include "forecast/pm25_history.h"

#include <math.h>
#include <string.h>

#define PM25_HISTORY_STRIDE 4U
#define PM25_HISTORY_MAX_GAP 3U

static size_t slot_index(uint64_t slot)
{
    return (size_t)((slot - 1U) % PM25_HISTORY_ROWS);
}

static uint64_t oldest_slot(uint64_t slot)
{
    return slot < PM25_HISTORY_ROWS ? 1U : slot - PM25_HISTORY_ROWS + 1U;
}

static void record_observation(pm25_history_t *history, uint64_t slot,
                               const float values[PM25_HISTORY_FEATURES])
{
    const uint64_t oldest = oldest_slot(slot);
    const uint64_t next_slot = history->last_slot + 1U;
    const uint64_t first_missing = next_slot > oldest ? next_slot : oldest;
    static const float missing[PM25_HISTORY_FEATURES] = {NAN, NAN, NAN};

    for (uint64_t skipped = first_missing; skipped < slot; ++skipped)
    {
        memcpy(history->observations.values[slot_index(skipped)], missing, sizeof(missing));
    }
    memcpy(history->observations.values[slot_index(slot)], values, sizeof(missing));
    history->last_slot = slot;
}

static void copy_window(pm25_history_t *history, uint64_t slot)
{
    const uint64_t first = oldest_slot(slot);
    for (size_t row = 0; row < PM25_HISTORY_ROWS; ++row)
    {
        memcpy(history->window.values[row], history->observations.values[slot_index(first + row)],
               sizeof(history->window.values[row]));
    }
}

static size_t next_observation(const pm25_window_t *window, size_t row, size_t feature)
{
    while (row < PM25_HISTORY_ROWS && !isfinite(window->values[row][feature]))
    {
        ++row;
    }
    return row;
}

static void interpolate_gap(pm25_window_t *window, size_t feature, size_t first, size_t right)
{
    const size_t left = first - 1U;
    const float start = window->values[left][feature];
    const float step = (window->values[right][feature] - start) / (float)(right - left);
    for (size_t row = first; row < right; ++row)
    {
        window->values[row][feature] = start + step * (float)(row - left);
    }
}

static bool fill_feature(pm25_window_t *window, size_t feature)
{
    size_t row = 0;
    while (row < PM25_HISTORY_ROWS)
    {
        if (isfinite(window->values[row][feature]))
        {
            ++row;
            continue;
        }

        const size_t right = next_observation(window, row + 1U, feature);
        if (row == 0 || right == PM25_HISTORY_ROWS || right - row > PM25_HISTORY_MAX_GAP)
        {
            return false;
        }
        interpolate_gap(window, feature, row, right);
        row = right + 1U;
    }
    return true;
}

bool pm25_history_record(pm25_history_t *history, uint64_t slot,
                         const float values[PM25_HISTORY_FEATURES])
{
    if (slot <= history->last_slot)
    {
        return false;
    }
    record_observation(history, slot, values);

    if (slot < PM25_HISTORY_ROWS || (slot - PM25_HISTORY_ROWS) % PM25_HISTORY_STRIDE != 0)
    {
        return false;
    }

    /* Nội suy trên bản sao để giữ nguồn quan sát gốc cho các cửa sổ sau. */
    copy_window(history, slot);
    for (size_t feature = 0; feature < PM25_HISTORY_FEATURES; ++feature)
    {
        if (!fill_feature(&history->window, feature))
        {
            return false;
        }
    }
    return true;
}
