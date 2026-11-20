#ifndef APP_NODE_IO_H
#define APP_NODE_IO_H

#include <stdbool.h>

#include "protocol/node_packet.h"

typedef struct
{
    int64_t due_since_init_us; /**< Mốc đo dự kiến tính từ cuối khởi tạo, theo µs. */
    uint32_t measurement_interval_ms; /**< Chu kỳ đo theo ms. */
    node_measurement_t temp_hum; /**< Mẫu nhiệt độ và độ ẩm. */
    bool temp_hum_valid; /**< Cho biết mẫu nhiệt độ và độ ẩm có hợp lệ. */
    uint16_t pm25_ug_m3; /**< PM2.5 môi trường. UINT16_MAX là thiếu, 0 hợp lệ. */
} node_forecast_sample_t;

/**
 * Các hàm I/O đồng bộ do tác vụ ứng dụng gọi tuần tự.
 * Bộ đệm chỉ được mượn trong thời gian gọi. Hàm đọc và đánh thức chỉ ghi
 * kết quả khi trả NODE_OK. Các mốc tuyệt đối dùng cùng đồng hồ với now_us.
 */
typedef struct
{
    node_result_t (*sensor_init)(void);
    node_result_t (*battery_init)(void);
    node_result_t (*hm3301_init)(bool enabled); /**< Khởi tạo HM3301 theo cấu hình. */
    node_result_t (*hm3301_wake)(uint32_t warmup_ms, int64_t *ready_at_us); /**< Bật SET và trả mốc đã đủ thời gian làm nóng. */
    node_result_t (*hm3301_read_pm25)(uint16_t *out_ug_m3); /**< Đọc một mẫu PM2.5 môi trường. */
    node_result_t (*hm3301_sleep)(void);
    node_result_t (*radio_init)(void); /**< Lỗi khởi tạo radio chặn ứng dụng. */
    node_result_t (*sensor_read)(node_measurement_t *out); /**< Đọc một mẫu mới. */
    node_result_t (*battery_read_mv)(uint16_t *out_mv); /**< Đọc điện áp pin đã hiệu chuẩn, theo mV. */
    /** Khi thành công, ghi mốc TX_DONE theo µs và chuyển sang RX trước khi trả về. */
    node_result_t (*radio_send)(const uint8_t *, size_t, uint32_t, int64_t *);
    node_result_t (*radio_receive)(uint8_t *, size_t, size_t *, int64_t); /**< Nhận khung trước hạn tuyệt đối. */
    node_result_t (*radio_sleep)(void);
    node_result_t (*sleep_ms)(uint32_t); /**< Cho MCU ngủ, có thể thức sớm. */
    node_result_t (*encode_packet)(const node_packet_t *, uint8_t *, size_t, size_t *); /**< Mã hóa vào bộ đệm do bên gọi cấp. */
    int64_t (*now_us)(void); /**< Đồng hồ đơn điệu, đơn vị µs. */
    void (*trace)(const char *, node_result_t, const uint8_t *, size_t); /**< Lưu nhật ký trong RAM. Khung NULL và độ dài 0 chỉ ghi trạng thái. */
    /** Tùy chọn. Gọi mỗi lượt bật HM3301 sau khi yêu cầu cảm biến ngủ.
     * Chỉ dùng kết quả khi trả true. */
    bool (*forecast_sample)(const node_forecast_sample_t *, uint16_t out[NODE_PM25_FORECAST_COUNT]);
} node_io_t;

#endif
