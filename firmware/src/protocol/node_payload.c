#include "protocol/node_payload.h"

/* DATA dùng hai số nguyên x10 và điện áp pin mV, không gửi float. */
node_result_t node_payload_encode_data(const node_measurement_t *sample,
                                       uint16_t battery_mv,
                                       uint8_t out[NODE_DATA_PAYLOAD_SIZE])
{
    uint16_t temperature_bits;

    if (sample == NULL || out == NULL)
    {
        return NODE_INVALID_ARG;
    }

    /* Chuyển số âm sang uint16_t lấy phần dư modulo 2^16, giữ mẫu bit 16 bit
     * để bên nhận có thể diễn giải lại thành nhiệt độ có dấu. */
    temperature_bits = (uint16_t)sample->temperature_x10;
    out[0] = (uint8_t)(temperature_bits >> 8);
    out[1] = (uint8_t)temperature_bits;
    /* Độ ẩm là số không dấu, cả hai trường đều gửi byte cao trước. */
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

    /* Chỉ giới hạn mã số tại đây, ý nghĩa từng mã không được suy ra từ codec. */
    if (error_code < 1U || error_code > 4U)
    {
        return NODE_OUT_OF_RANGE;
    }

    out[0] = error_code;
    return NODE_OK;
}
