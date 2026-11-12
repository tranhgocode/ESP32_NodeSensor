#include <stdbool.h>
#include <stddef.h>

#include "board_config.h"
#include "node_config.h"
#include "sensor/battery.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define BATTERY_ADC_RAW_MAX 4095

_Static_assert(NODE_BATTERY_SAMPLE_COUNT > 0, "Battery sample count must be positive");
_Static_assert(NODE_BATTERY_DIVIDER_NUMERATOR > 0, "Battery divider must be positive");
_Static_assert(NODE_BATTERY_DIVIDER_DENOMINATOR > 0, "Battery divider must be positive");

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t calibration_handle;
static adc_channel_t adc_channel;
static bool initialized;

static node_result_t release_resources(void)
{
    node_result_t result = NODE_OK;
    initialized = false;
    if (calibration_handle != NULL)
    {
        if (adc_cali_delete_scheme_curve_fitting(calibration_handle) == ESP_OK)
        {
            calibration_handle = NULL;
        }
        else
        {
            result = NODE_INIT_FAILED;
        }
    }
    if (adc_handle != NULL)
    {
        if (adc_oneshot_del_unit(adc_handle) == ESP_OK)
        {
            adc_handle = NULL;
        }
        else
        {
            result = NODE_INIT_FAILED;
        }
    }
    return result;
}

node_result_t battery_init(void)
{
    if (release_resources() != NODE_OK)
    {
        return NODE_INIT_FAILED;
    }
    adc_unit_t unit;
    if (adc_oneshot_io_to_channel(BOARD_BATTERY_GPIO, &unit, &adc_channel) != ESP_OK ||
        unit != ADC_UNIT_1)
    {
        return NODE_INIT_FAILED;
    }
    const adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&unit_config, &adc_handle) != ESP_OK)
    {
        return NODE_INIT_FAILED;
    }
    const adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    const adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = unit,
        .chan = adc_channel,
        .atten = channel_config.atten,
        .bitwidth = channel_config.bitwidth,
    };
    if (adc_oneshot_config_channel(adc_handle, adc_channel, &channel_config) != ESP_OK ||
        adc_cali_create_scheme_curve_fitting(&calibration_config, &calibration_handle) != ESP_OK)
    {
        /* Giữ lại handle nếu cleanup thất bại, không cấp thêm khi còn tài nguyên cũ. */
        const node_result_t cleanup_result = release_resources();
        if (cleanup_result != NODE_OK)
        {
            return cleanup_result;
        }
        return NODE_INIT_FAILED;
    }
    initialized = true;
    return NODE_OK;
}

node_result_t battery_read_mv(uint16_t *out_mv)
{
    if (out_mv == NULL)
    {
        return NODE_INVALID_ARG;
    }
    if (!initialized)
    {
        return NODE_INIT_FAILED;
    }

    uint64_t total_mv = 0;
    for (unsigned i = 0; i < NODE_BATTERY_SAMPLE_COUNT; ++i)
    {
        int raw = 0;
        const esp_err_t read_result = adc_oneshot_read(adc_handle, adc_channel, &raw);
        if (read_result != ESP_OK)
        {
            return read_result == ESP_ERR_TIMEOUT ? NODE_TIMEOUT : NODE_IO_ERROR;
        }
        if (raw < 0 || raw >= BATTERY_ADC_RAW_MAX)
        {
            return NODE_OUT_OF_RANGE;
        }
        int millivolts = 0;
        if (adc_cali_raw_to_voltage(calibration_handle, raw, &millivolts) != ESP_OK)
        {
            return NODE_IO_ERROR;
        }
        if (millivolts < 0)
        {
            return NODE_OUT_OF_RANGE;
        }
        total_mv += (uint64_t)millivolts;
    }

    /* Chỉ làm tròn một lần sau lấy trung bình và chia áp, tránh mất nửa mV. */
    const uint64_t divisor = (uint64_t)NODE_BATTERY_SAMPLE_COUNT *
                             NODE_BATTERY_DIVIDER_DENOMINATOR;
    const uint64_t battery_mv = (total_mv * NODE_BATTERY_DIVIDER_NUMERATOR + divisor / 2) /
                                divisor;
    if (battery_mv >= UINT16_MAX)
    {
        return NODE_OUT_OF_RANGE;
    }
    *out_mv = (uint16_t)battery_mv;
    return NODE_OK;
}
