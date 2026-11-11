#ifndef INDICATOR_STATUS_LED_H
#define INDICATOR_STATUS_LED_H
#include <stdint.h>
#include "node_types.h"
/**
 * Khởi tạo LED thường tại GPIO15: HIGH bật, LOW tắt.
 * Khởi tạo thành công để LED ở trạng thái tắt. Các API do một tác vụ sở hữu.
 * @return NODE_OK khi thành công, NODE_INIT_FAILED nếu GPIO không hợp lệ
 *         hoặc một bước khởi tạo thất bại.
 */
node_result_t status_led_init(void);
/**
 * Bật/tắt LED một màu, giữ chữ ký ba kênh theo hợp đồng Task 6.
 * Bất kỳ kênh nào khác 0 đều bật LED, cả ba bằng 0 thì tắt LED.
 * LED này không hỗ trợ màu RGB hoặc điều chỉnh độ sáng bằng PWM.
 *
 * @param red Giá trị kênh đỏ, khác 0 thì bật LED.
 * @param green Giá trị kênh xanh lá, khác 0 thì bật LED.
 * @param blue Giá trị kênh xanh dương, khác 0 thì bật LED.
 * @return NODE_OK khi ghi GPIO thành công, NODE_INIT_FAILED nếu chưa
 *         khởi tạo thành công, NODE_IO_ERROR nếu ghi GPIO thất bại.
 */
node_result_t status_led_set(uint8_t red, uint8_t green, uint8_t blue);
/** Tắt LED bằng cách ghi LOW, mã trả về giống status_led_set(0, 0, 0). */
node_result_t status_led_off(void);
#endif
