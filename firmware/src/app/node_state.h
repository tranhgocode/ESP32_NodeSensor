#ifndef APP_NODE_STATE_H
#define APP_NODE_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "node_config.h"
#include "node_types.h"
#include "protocol/node_packet.h"

/** Trạng thái logic, không tự chạy tác vụ đo, phát hay ngủ phần cứng. */
typedef enum
{
    NODE_STATE_SLEEP = 0,
    NODE_STATE_MEASURE,
    NODE_STATE_TRANSMIT,
    NODE_STATE_WAIT_ACK,
    NODE_STATE_FINISH,
} node_state_t;

/**
 * Trạng thái của một node, do duy nhất task ứng dụng sở hữu.
 *
 * ID và Seq nhận diện lượt hiện tại, tăng sau khi kết thúc lượt. Context nằm
 * trong RAM, Light-sleep giữ RAM còn reset thông thường gọi init lại. Không
 * có handle driver hay con trỏ mượn tới config, nên module portable trên host.
 */
typedef struct
{
    uint8_t id;                       /**< Mã lượt 8 bit, tràn 255 về 0. */
    uint16_t seq;                     /**< Số thứ tự 16 bit, tràn 65535 về 0. */
    node_state_t state;              /**< Caller đổi sang MEASURE/TRANSMIT khi làm I/O. */
    uint8_t node_addr;               /**< Địa chỉ đích bắt buộc của ACK. */
    uint8_t gateway_addr;            /**< Địa chỉ nguồn bắt buộc của ACK. */
    uint32_t ack_timeout_ms;          /**< Bản sao timeout từ config, đơn vị ms. */
    uint32_t tx_timeout_ms;          /**< Bản sao cấu hình để từ chối thay đổi sau init. */
    uint32_t measurement_interval_ms; /**< Chu kỳ giữa các mốc đo, đơn vị ms. */
    uint32_t hm3301_warmup_ms;       /**< Thời gian chuẩn bị cảm biến bụi, đơn vị ms. */
    bool hm3301_enabled;            /**< Chọn cảm biến bụi và schema DATA8 cho cả lượt. */
    int64_t last_measurement_due_us;  /**< Mốc dự kiến, không phải thời điểm TX. */
    node_result_t hm3301_init_result; /**< Giữ lỗi init để không tự khởi tạo lại driver. */
    int64_t tx_done_us;               /**< Mốc TX_DONE của lượt hiện tại, đơn vị us. */
    int64_t ack_deadline_us;          /**< Deadline tuyệt đối, ACK đúng mốc này đã trễ. */
    node_result_t sensor_init_result; /**< App lưu kết quả init sensor, state không gọi sensor. */
    node_result_t battery_init_result; /**< App lưu kết quả init pin, lỗi pin không chặn DATA. */
    bool app_initialized;            /**< App bật sau radio init thành công, state init đặt false. */
} node_context_t;

/**
 * Khởi tạo hoặc reset context: ID/Seq = 0, SLEEP, chưa có cửa sổ chờ ACK.
 *
 * @param ctx Context ghi được, do caller sở hữu, NULL thì không làm gì.
 * @param config Config đọc được, được chép các giá trị cần thiết, NULL thì
 *               giữ nguyên ctx. Ứng dụng chịu trách nhiệm kiểm tra địa chỉ cấu hình.
 * Kết quả init sensor và pin bắt đầu NODE_INIT_FAILED, app thay bằng kết
 * quả init thật trước khi chạy lượt đầu, không coi đây là thao tác driver.
 */
void node_state_init(node_context_t *ctx, const node_config_t *config);

/**
 * Mở cửa sổ ACK sau một TX thành công, gọi đúng một lần cho mỗi TX_DONE.
 *
 * @param ctx Context đã init, NULL thì không làm gì.
 * @param tx_done_us Thời gian monotonic TX_DONE, cùng clock với now_us của
 *                   accept_ack. Deadline = tx_done_us + ack_timeout_ms * 1000.
 * Mốc âm hoặc tổng vượt INT64_MAX đóng cửa sổ, đưa state về FINISH để caller
 * kết thúc lượt. Không tự đọc clock, chờ ACK, gửi lại hoặc tăng counter ở đây.
 */
void node_state_begin_ack_wait(node_context_t *ctx, int64_t tx_done_us);

/**
 * Kiểm tra ACK, chỉ đọc dữ liệu, không đổi state, counters hay deadline.
 *
 * @param ctx Context đã init.
 * @param packet Packet đã được codec decode và xác minh CRC thành công.
 * @param now_us Thời điểm chấp nhận sau decode, cùng clock với TX_DONE.
 * @return true khi WAIT_ACK, ACK rỗng, Src/Dest/ID/Seq khớp và thời gian nằm
 *         trong [tx_done_us, ack_deadline_us). NULL hoặc ACK sai/trễ trả false.
 * Caller quyết định kết thúc lượt sau true, gói sai không cấp thêm thời gian.
 */
bool node_state_accept_ack(const node_context_t *ctx,
                           const node_packet_t *packet,
                           int64_t now_us);

/**
 * Kết thúc lượt thành công hoặc thất bại, tăng ID/Seq theo độ rộng từng trường
 * rồi về SLEEP và xóa cửa sổ ACK. Giữ cấu hình và kết quả init sensor/pin qua lượt.
 *
 * @param ctx Context của lượt đã bắt đầu (state khác SLEEP).
 * NULL hoặc state đã SLEEP thì không làm gì, tránh tăng lại nếu cleanup gọi
 * hai lần. Caller phải đặt MEASURE/TRANSMIT khi bắt đầu lượt kế tiếp. Hàm này
 * không gọi sleep thật, ứng dụng điều phối I/O và thời gian ngủ.
 */
void node_state_finish_cycle(node_context_t *ctx);

#endif
