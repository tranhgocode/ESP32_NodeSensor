#ifndef PROTOCOL_NODE_PAYLOAD_H
#define PROTOCOL_NODE_PAYLOAD_H

#include <stdint.h>

#include "node_types.h"
#include "protocol/node_packet.h"

#define NODE_BATTERY_MV_UNAVAILABLE UINT16_MAX
#define NODE_PM25_UNAVAILABLE UINT16_MAX

/**
 * Mã hóa DATA16 bằng cách nối bốn dự báo PM2.5 sau DATA8.
 * @param sample Mẫu nhiệt độ và độ ẩm.
 * @param battery_mv Điện áp pin theo mV hoặc NODE_BATTERY_MV_UNAVAILABLE.
 * @param pm25_ug_m3 PM2.5 đo được hoặc NODE_PM25_UNAVAILABLE.
 * @param forecast Bốn giá trị +1 đến +4 bước, theo µg/m³, ghi byte cao trước.
 * @param out Bộ đệm có ít nhất 16 byte ghi được.
 * @return NODE_OK hoặc NODE_INVALID_ARG nếu thiếu con trỏ. Lỗi không sửa out.
 */
node_result_t node_payload_encode_data_forecast(
    const node_measurement_t *sample, uint16_t battery_mv, uint16_t pm25_ug_m3,
    const uint16_t forecast[NODE_PM25_FORECAST_COUNT],
    uint8_t out[NODE_DATA_FORECAST_PAYLOAD_SIZE]);

/**
 * Mã hóa DATA8 với PM2.5 ở byte 6 và 7, theo µg/m³, byte cao trước.
 * @param sample Mẫu nhiệt độ và độ ẩm.
 * @param battery_mv Điện áp pin theo mV hoặc NODE_BATTERY_MV_UNAVAILABLE.
 * @param pm25_ug_m3 PM2.5 môi trường. UINT16_MAX là thiếu, 0 hợp lệ.
 * @param out Bộ đệm có ít nhất 8 byte ghi được.
 * @return NODE_OK hoặc NODE_INVALID_ARG nếu thiếu con trỏ. Lỗi không sửa out.
 */
node_result_t node_payload_encode_data_pm25(const node_measurement_t *sample,
                                            uint16_t battery_mv, uint16_t pm25_ug_m3,
                                            uint8_t out[NODE_DATA_PM25_PAYLOAD_SIZE]);

/**
 * Mã hóa DATA6: nhiệt độ ×10 có dấu, độ ẩm ×10 không dấu và pin theo mV.
 * Mỗi trường chiếm hai byte, ghi byte cao trước.
 * @param sample Mẫu nhiệt độ và độ ẩm cần mã hóa.
 * @param battery_mv Điện áp pin trước chia áp hoặc NODE_BATTERY_MV_UNAVAILABLE.
 * @param out Bộ đệm có ít nhất 6 byte ghi được.
 * @return NODE_OK hoặc NODE_INVALID_ARG nếu thiếu con trỏ. Lỗi không sửa out.
 */
node_result_t node_payload_encode_data(const node_measurement_t *sample,
                                       uint16_t battery_mv,
                                       uint8_t out[NODE_DATA_PAYLOAD_SIZE]);

/**
 * Mã hóa mã lỗi thành một byte.
 * @param error_code Mã ERROR trong khoảng 1..4.
 * @param out Bộ đệm có ít nhất một byte ghi được.
 * @return NODE_OK, NODE_INVALID_ARG nếu out là NULL hoặc NODE_OUT_OF_RANGE
 *         nếu mã nằm ngoài miền hợp lệ. Lỗi không sửa out.
 */
node_result_t node_payload_encode_error(uint8_t error_code, uint8_t out[1]);

#endif
