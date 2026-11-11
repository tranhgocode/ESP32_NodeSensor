#ifndef APP_NODE_IO_H
#define APP_NODE_IO_H

#include "protocol/node_packet.h"

/**
 * Biên I/O đồng bộ, chỉ một task ứng dụng gọi tuần tự.
 * Callback không giữ các con trỏ buffer sau khi trả về.
 * Send trả timestamp TX_DONE và đã bật RX khi thành công.
 * Receive dùng deadline tuyệt đối cùng clock monotonic với now_us.
 * Trace nhận buffer mượn, NULL và len=0 khi chỉ báo trạng thái.
 * Tất cả callback là bắt buộc, encode_packet thường là node_packet_encode.
 */
typedef struct
{
    node_result_t (*sensor_init)(void);
    node_result_t (*radio_init)(void);
    node_result_t (*sensor_read)(node_measurement_t *out);
    node_result_t (*radio_send)(const uint8_t *, size_t, uint32_t, int64_t *);
    node_result_t (*radio_receive)(uint8_t *, size_t, size_t *, int64_t);
    node_result_t (*radio_sleep)(void);
    node_result_t (*sleep_ms)(uint32_t);
    node_result_t (*encode_packet)(const node_packet_t *, uint8_t *, size_t, size_t *);
    int64_t (*now_us)(void);
    void (*trace)(const char *, node_result_t, const uint8_t *, size_t);
} node_io_t;

#endif
