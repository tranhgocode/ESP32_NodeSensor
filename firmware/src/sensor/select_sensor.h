#ifndef SENSOR_SELECT_SENSOR_H
#define SENSOR_SELECT_SENSOR_H

#include "node_types.h"

/* Cấu hình build quyết định driver để lỗi cảm biến không đổi nguồn dữ liệu.
 * Init/read chuyển nguyên kết quả driver, read chỉ đổi out khi NODE_OK.
 * Name trả chuỗi hằng, caller không sở hữu hoặc giải phóng chuỗi này.
 */
node_result_t node_sensor_init(void);
node_result_t node_sensor_read(node_measurement_t *out);
const char *node_sensor_name(void);

#endif
