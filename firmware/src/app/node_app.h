#ifndef APP_NODE_APP_H
#define APP_NODE_APP_H

#include "app/node_io.h"
#include "app/node_state.h"

/**
 * Caller sở hữu ctx/config/io suốt thời gian chạy, chỉ một task gọi app.
 * Init reset counter và khởi tạo radio/cảm biến, không ngủ hoặc gửi gói.
 * Radio lỗi chặn run_cycle, sensor lỗi được lưu để gửi ERROR ở lượt sau.
 * Config phải có địa chỉ node 1..254 khác gateway, gateway khác 255,
 * các timeout và sleep khác 0. Không đổi config/io giữa các lời gọi.
 */
node_result_t node_app_init(node_context_t *ctx, const node_config_t *config,
                            const node_io_t *io);

/**
 * Ngủ trước khi đo, kể cả lượt đầu, phát tối đa một gói, không retry.
 * ACK đúng trước deadline trả OK, timeout/lỗi trả mã tương ứng.
 * Mỗi lượt đã bắt đầu đo tăng counter đúng một lần kể cả khi thất bại.
 * Lỗi chuẩn bị radio/ngủ trước đo không bắt đầu lượt và không tăng counter.
 * Radio ngủ lại sau lượt, lỗi cleanup trả về nếu lượt chưa có lỗi khác.
 * Mẫu chỉ dùng khi read trả OK, OUT_OF_RANGE ánh xạ ERROR=03.
 * Lỗi cảm biến vẫn trả OK nếu gói ERROR được gateway xác nhận.
 */
node_result_t node_app_run_cycle(node_context_t *ctx, const node_config_t *config,
                                 const node_io_t *io);

#endif
