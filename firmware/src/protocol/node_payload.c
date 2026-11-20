#include "protocol/node_payload.h"

node_result_t node_payload_encode_data_forecast(
    const node_measurement_t *sample, uint16_t battery_mv, uint16_t pm25_ug_m3,
    const uint16_t forecast[NODE_PM25_FORECAST_COUNT],
    uint8_t out[NODE_DATA_FORECAST_PAYLOAD_SIZE])
{
    if (forecast == NULL)
    {
        return NODE_INVALID_ARG;
    }
    const node_result_t result = node_payload_encode_data_pm25(sample, battery_mv, pm25_ug_m3, out);
    if (result != NODE_OK)
    {
        return result;
    }
    for (size_t step = 0; step < NODE_PM25_FORECAST_COUNT; ++step)
    {
        const size_t offset = NODE_DATA_PM25_PAYLOAD_SIZE + 2U * step;
        out[offset] = (uint8_t)(forecast[step] >> 8);
        out[offset + 1] = (uint8_t)forecast[step];
    }
    return NODE_OK;
}

node_result_t node_payload_encode_data_pm25(const node_measurement_t *sample,
                                            uint16_t battery_mv, uint16_t pm25_ug_m3,
                                            uint8_t out[NODE_DATA_PM25_PAYLOAD_SIZE])
{
    const node_result_t result = node_payload_encode_data(sample, battery_mv, out);
    if (result != NODE_OK)
    {
        return result;
    }
    out[6] = (uint8_t)(pm25_ug_m3 >> 8);
    out[7] = (uint8_t)pm25_ug_m3;
    return NODE_OK;
}

node_result_t node_payload_encode_data(const node_measurement_t *sample,
                                       uint16_t battery_mv,
                                       uint8_t out[NODE_DATA_PAYLOAD_SIZE])
{
    uint16_t temperature_bits;

    if (sample == NULL || out == NULL)
    {
        return NODE_INVALID_ARG;
    }

    /* Giữ nguyên mẫu bit 16 bit của nhiệt độ có dấu để bên nhận giải mã. */
    temperature_bits = (uint16_t)sample->temperature_x10;
    out[0] = (uint8_t)(temperature_bits >> 8);
    out[1] = (uint8_t)temperature_bits;
    out[2] = (uint8_t)(sample->humidity_x10 >> 8);
    out[3] = (uint8_t)sample->humidity_x10;
    out[4] = (uint8_t)(battery_mv >> 8);
    out[5] = (uint8_t)battery_mv;

    return NODE_OK;
}

node_result_t node_payload_encode_error(uint8_t error_code, uint8_t out[1])
{
    if (out == NULL)
    {
        return NODE_INVALID_ARG;
    }

    /* Bộ mã hóa chỉ kiểm tra miền mã ERROR. Ứng dụng quyết định ý nghĩa từng mã. */
    if (error_code < 1U || error_code > 4U)
    {
        return NODE_OUT_OF_RANGE;
    }

    out[0] = error_code;
    return NODE_OK;
}
