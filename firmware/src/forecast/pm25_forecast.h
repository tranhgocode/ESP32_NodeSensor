#ifndef FORECAST_PM25_FORECAST_H
#define FORECAST_PM25_FORECAST_H

#include "app/node_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Khởi tạo TFLite Micro một lần trước lịch đo. Lỗi khởi tạo làm dự báo không khả dụng. */
void pm25_forecast_init(void);

/**
 * Thử suy luận trên 24 mốc đo, bắt đầu ở mốc 24 rồi trượt mỗi 4 mốc.
 * Nội suy từng đặc trưng khi có đủ hai số đo bao quanh khoảng thiếu tối đa 3 mốc.
 * @param sample Mẫu theo mốc đo. Phải khác NULL.
 * @param out Mảng đích có chỗ cho bốn giá trị PM2.5 theo µg/m³,
 *            theo thứ tự bốn bước đầu ra của mô hình.
 * @return true nếu có đủ bốn dự báo hợp lệ. Chỉ dùng out khi trả true.
 */
bool pm25_forecast_sample(const node_forecast_sample_t *sample,
                          uint16_t out[NODE_PM25_FORECAST_COUNT]);

/** In và xóa sự kiện dự báo sau khi kết thúc lượt LoRa. */
void pm25_forecast_flush_log(void);

#ifdef __cplusplus
}
#endif

#endif
