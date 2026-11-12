#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* Chân dữ liệu DHT11, dùng module 3.3 V có điện trở kéo lên trên đường DATA.
 * Các giá trị bên dưới là số GPIO của MCU. */
#define BOARD_DHT11_GPIO 7

/* Đầu vào ADC1_CH0 để đo điện áp pin qua mạch chia áp. */
#define BOARD_BATTERY_GPIO 1

/* Bus I2C đọc PM2.5 từ HM3301. */
#define BOARD_HM3301_SDA_GPIO 8
#define BOARD_HM3301_SCL_GPIO 9
#define BOARD_HM3301_SET_GPIO 14    /* SET LOW cho cảm biến ngủ, SET HIGH bật quạt và bắt đầu warmup. */
#define BOARD_HM3301_RESET_GPIO 16  /* Chân reset HM3301, firmware hiện không cấu hình hoặc điều khiển chân này. */


/* Bus SPI2 giao tiếp radio Ra-02, tách khỏi SPI1 dùng cho flash và PSRAM. */
#define BOARD_RADIO_CS_GPIO 10
#define BOARD_RADIO_MOSI_GPIO 11
#define BOARD_RADIO_SCK_GPIO 12
#define BOARD_RADIO_MISO_GPIO 13
#define BOARD_RADIO_DIO0_GPIO 4     /* DIO0 báo TX_DONE hoặc RX_DONE. */
#define BOARD_RADIO_RESET_GPIO 5    /* RESET khởi tạo radio. */
#define BOARD_RADIO_DIO1_GPIO 6     /* DIO1 dự phòng, firmware hiện không cấu hình hoặc bật ngắt. */


/* LED trạng thái thông thường, HIGH bật và LOW tắt. */
#define BOARD_STATUS_LED_GPIO 15

#endif
