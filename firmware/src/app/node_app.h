#ifndef APP_NODE_APP_H
#define APP_NODE_APP_H

#include "app/node_io.h"
#include "app/node_state.h"

/**
 * Caller sở hữu ctx/config/io suốt thời gian chạy, chỉ một task gọi app.
 * Init reset counter và khởi tạo radio/cảm biến, không ngủ hoặc gửi gói.
 * Radio lỗi chặn run_cycle, sensor lỗi được lưu để gửi ERROR ở lượt sau.
 * Config phải có địa chỉ node 1..254 khác gateway, gateway khác 255,
 * chu kỳ đo và timeout khác 0. Khi HM3301 ON, warmup ít nhất 30000 ms,
 * chu kỳ lớn hơn warmup. Không đổi config/io giữa các lời gọi.
 */
node_result_t node_app_init(node_context_t *ctx, const node_config_t *config,
                            const node_io_t *io);

/**
 * Bám mốc chu kỳ từ cuối init, ngủ tới trước warmup rồi ngủ tiếp khi HM ON.
 * Mốc trễ không đủ chuẩn bị được bỏ qua, phát tối đa một gói, không retry.
 * ACK đúng trước deadline trả OK, timeout/lỗi trả mã tương ứng.
 * Mỗi lượt đã bắt đầu đo tăng counter đúng một lần kể cả khi thất bại.
 * Lỗi chuẩn bị radio/ngủ trước đo không bắt đầu lượt và không tăng counter.
 * Radio ngủ lại sau lượt, lỗi cleanup trả về nếu lượt chưa có lỗi khác.
 * Mẫu chỉ dùng khi read trả OK, OUT_OF_RANGE ánh xạ ERROR=03.
 * Lỗi cảm biến vẫn trả OK nếu gói ERROR được gateway xác nhận.
 * HM3301 lỗi dùng PM unavailable trong DATA8, OFF giữ DATA6.
 */
node_result_t node_app_run_cycle(node_context_t *ctx, const node_config_t *config,
                                 const node_io_t *io);

#endif
