#ifndef FORECAST_PM25_FORECAST_H
#define FORECAST_PM25_FORECAST_H

#include "app/node_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Khởi tạo TFLite Micro một lần trước lịch đo. Lỗi khởi tạo làm dự báo không khả dụng. */
void pm25_forecast_init(void);

/**
 * Gom mẫu vào các cửa sổ 24 hàng tính từ cuối khởi tạo.
 * Mỗi hàng ứng với một giờ ở chế độ thường hoặc một mốc đo ở chế độ demo.
 * @param sample Mẫu theo mốc đo. Phải khác NULL.
 * @param out Mảng đích có chỗ cho bốn giá trị PM2.5 theo µg/m³,
 *            tương ứng 1 đến 4 giờ tiếp theo, hoặc 1 đến 4 mốc đo ở chế độ demo.
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
