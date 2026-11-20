#ifndef APP_NODE_APP_H
#define APP_NODE_APP_H

#include "app/node_io.h"
#include "app/node_state.h"

/**
 * Khởi tạo trình điều khiển và chốt mốc bắt đầu lịch đo.
 * Chỉ tác vụ ứng dụng gọi API này. Lỗi radio dừng khởi tạo, lỗi cảm biến
 * hoặc pin được lưu để xử lý trong chu kỳ đo.
 *
 * @param ctx Trạng thái do bên gọi cấp và sở hữu.
 * @param config Cấu hình cố định. Địa chỉ node là 1..254 và khác gateway.
 *               Gateway khác 255, chu kỳ và các thời gian chờ phải dương.
 *               Nếu bật HM3301, thời gian làm nóng từ 30 giây và ngắn hơn chu kỳ.
 * @param io Bảng hàm I/O cố định. Chỉ forecast_sample có thể để NULL.
 * @return NODE_OK, lỗi radio, NODE_INVALID_ARG hoặc NODE_IO_ERROR nếu đồng hồ lỗi.
 */
node_result_t node_app_init(node_context_t *ctx, const node_config_t *config,
                            const node_io_t *io);

/**
 * Chạy một chu kỳ đo, gửi tối đa một gói DATA hoặc ERROR và chờ ACK.
 * Bỏ qua mốc đã lỡ. Với HM3301, chờ đến mốc đo và đủ thời gian làm nóng.
 * Phần dữ liệu của DATA dài 6, 8 hoặc 16 byte tùy nội dung.
 * ID và Seq tăng một lần sau khi đã bắt đầu đo, kể cả khi lượt lỗi.
 *
 * @param ctx Trạng thái đã khởi tạo bằng node_app_init.
 * @param config Cấu hình đã dùng khi khởi tạo.
 * @param io Bảng hàm I/O đã dùng khi khởi tạo.
 * @return NODE_OK nếu nhận ACK khớp trước hạn và đưa radio về Sleep thành công.
 *         Nếu thất bại, trả mã lỗi tương ứng, kể cả NODE_TIMEOUT.
 */
node_result_t node_app_run_cycle(node_context_t *ctx, const node_config_t *config,
                                 const node_io_t *io);

#endif
