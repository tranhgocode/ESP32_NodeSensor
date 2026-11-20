#ifndef APP_NODE_STATE_H
#define APP_NODE_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "node_config.h"
#include "node_types.h"
#include "protocol/node_packet.h"

/** Trạng thái logic của một chu kỳ đo. */
typedef enum
{
    NODE_STATE_SLEEP = 0,
    NODE_STATE_MEASURE,
    NODE_STATE_TRANSMIT,
    NODE_STATE_WAIT_ACK,
    NODE_STATE_FINISH,
} node_state_t;

/** Trạng thái của node do tác vụ ứng dụng sở hữu và được giữ qua Light-sleep. */
typedef struct
{
    uint8_t id;                       /**< ID 8 bit, quay về 0 sau 255. */
    uint16_t seq;                     /**< Seq 16 bit, quay về 0 sau 65535. */
    node_state_t state;              /**< Ứng dụng đặt MEASURE và TRANSMIT khi thực hiện I/O. */
    uint8_t node_addr;               /**< Địa chỉ đích của ACK. */
    uint8_t gateway_addr;            /**< Địa chỉ nguồn của ACK. */
    uint32_t ack_timeout_ms;          /**< Giới hạn chờ ACK, theo ms. */
    uint32_t tx_timeout_ms;          /**< Giới hạn chờ phát, theo ms. */
    uint32_t measurement_interval_ms; /**< Chu kỳ đo, theo ms. */
    uint32_t hm3301_warmup_ms;       /**< Thời gian làm nóng HM3301, theo ms. */
    bool hm3301_enabled;            /**< Bật đo và truyền PM2.5. */
    int64_t last_measurement_due_us;  /**< Mốc đo dự kiến gần nhất, theo µs. */
    int64_t measurement_epoch_us;     /**< Mốc cuối khởi tạo dùng để tính thời gian tương đối cho dự báo. */
    node_result_t hm3301_init_result; /**< Kết quả khởi tạo HM3301, được giữ qua các lượt. */
    int64_t tx_done_us;               /**< Mốc TX_DONE của lượt hiện tại, theo µs. */
    int64_t ack_deadline_us;          /**< Hạn chờ tuyệt đối. ACK tại đúng mốc này đã trễ. */
    node_result_t sensor_init_result; /**< Kết quả khởi tạo cảm biến nhiệt độ và độ ẩm. */
    node_result_t battery_init_result; /**< Lỗi khởi tạo pin không chặn gói DATA. */
    bool app_initialized;            /**< Ứng dụng đặt true khi khởi tạo xong. */
} node_context_t;

/**
 * Đặt ID và Seq về 0, chuyển sang SLEEP và xóa cửa sổ ACK.
 * @param ctx Trạng thái do bên gọi cấp.
 * @param config Cấu hình nguồn. Hàm bỏ qua nếu ctx hoặc config là NULL.
 */
void node_state_init(node_context_t *ctx, const node_config_t *config);

/**
 * Mở cửa sổ ACK một lần sau TX_DONE. Hạn bằng TX_DONE cộng thời gian chờ ACK.
 * Mốc âm hoặc phép cộng bị tràn đưa lượt về FINISH.
 * @param ctx Trạng thái đã khởi tạo. Bỏ qua nếu NULL.
 * @param tx_done_us Mốc TX_DONE theo cùng đồng hồ với thời điểm kiểm tra ACK.
 */
void node_state_begin_ack_wait(node_context_t *ctx, int64_t tx_done_us);

/**
 * Kiểm tra ACK đã xác minh CRC mà không thay đổi trạng thái.
 * @param ctx Trạng thái đang chờ ACK.
 * @param packet Gói đã giải mã.
 * @param now_us Mốc kiểm tra sau giải mã, cùng đồng hồ với TX_DONE.
 * @return true nếu gói ACK rỗng khớp Src, Dest, ID và Seq trong [TX_DONE, hạn chờ).
 */
bool node_state_accept_ack(const node_context_t *ctx,
                           const node_packet_t *packet,
                           int64_t now_us);

/**
 * Tăng ID và Seq, xóa cửa sổ ACK rồi chuyển về SLEEP.
 * @param ctx Trạng thái của lượt đã bắt đầu. Bỏ qua nếu NULL hoặc đã SLEEP.
 */
void node_state_finish_cycle(node_context_t *ctx);

#endif
