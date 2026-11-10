#ifndef NODE_TYPES_H
#define NODE_TYPES_H

#include <stdint.h>

/**
 * Mã kết quả nội bộ dùng để báo trạng thái giữa các module.
 * Các giá trị này không được ghi trực tiếp vào payload ERROR trên dây; mã
 * ERROR hợp lệ của giao thức là các số 1 đến 4.
 */
typedef enum
{
    NODE_OK = 0,             /**< Thao tác hoàn tất. */
    NODE_INVALID_ARG,        /**< Thiếu con trỏ bắt buộc hoặc đối số không hợp lệ. */
    NODE_INVALID_FRAME,      /**< Độ dài hoặc cấu trúc gói tin không hợp lệ. */
    NODE_BUFFER_TOO_SMALL,   /**< Bộ đệm đích không đủ chỗ để mã hóa. */
    NODE_INIT_FAILED,        /**< Khởi tạo thành phần không thành công. */
    NODE_TIMEOUT,            /**< Một thao tác chờ vượt quá thời hạn. */
    NODE_CHECKSUM_ERROR,     /**< CRC của khung không khớp. */
    NODE_OUT_OF_RANGE,       /**< Giá trị nằm ngoài miền mà API chấp nhận. */
    NODE_IO_ERROR,           /**< Thao tác vào/ra không thành công. */
    NODE_ENCODE_FAILED,      /**< Mã hóa thất bại ở tầng gọi. */
} node_result_t;

/**
 * Một mẫu đo ở dạng logic trong RAM, chưa phải payload đã tuần tự hóa.
 * Hai trường dùng đơn vị phần mười: nhiệt độ có dấu, độ ẩm không dấu.
 */
typedef struct
{
    int16_t temperature_x10; /**< Nhiệt độ nhân 10; số âm được giữ nguyên dấu. */
    uint16_t humidity_x10;   /**< Độ ẩm nhân 10; miền lưu trữ là 0..65535. */
} node_measurement_t;

#endif
