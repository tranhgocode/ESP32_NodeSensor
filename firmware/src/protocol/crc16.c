#include "protocol/crc16.h"

/* Tham số CRC-16/CCITT-FALSE: init=0xFFFF, poly=0x1021, không phản chiếu,
 * và không XOR giá trị cuối. Các byte được đưa vào từ bit cao trước. */
#define CRC16_CCITT_FALSE_INITIAL 0xFFFFU
#define CRC16_CCITT_FALSE_POLYNOMIAL 0x1021U

uint16_t crc16_ccitt_false(const uint8_t *data, size_t len)
{
    uint16_t crc = CRC16_CCITT_FALSE_INITIAL;
    size_t index;

    if (data == NULL)
    {
        /* API không có mã lỗi riêng; con trỏ NULL cho kết quả khởi tạo. */
        return crc;
    }

    for (index = 0; index < len; ++index)
    {
        uint8_t bit;

        /* Trộn byte vào nửa cao để xử lý theo thứ tự bit MSB trước. */
        crc ^= (uint16_t)((uint16_t)data[index] << 8);
        for (bit = 0; bit < 8; ++bit)
        {
            /* Xét bit 15 trước khi dịch; cast giữ lại đúng 16 bit thấp. Nếu
             * bit đó bằng 1, XOR thêm đa thức 0x1021 sau phép dịch trái. */
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1) ^ CRC16_CCITT_FALSE_POLYNOMIAL);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}
