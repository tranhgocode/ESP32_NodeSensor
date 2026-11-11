#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* Người dùng xác nhận DATA GPIO7, nguồn 3.3V, module DHT11 ba chân có pull-up.
 * Schematic YD-ESP32-S3 V1.4: net GPIO7 đi tới U4 IO7 và J1 chân 7.
 * Đây là số GPIO, không phải chỉ số chân header của một board bất kỳ.
 * Chỉ bổ sung chân cho thiết bị khác sau khi xác nhận dây nối riêng. */
#define BOARD_DHT11_GPIO 7

/* Pin Ra-02 do người dùng cung cấp, đối chiếu schematic YD V1.4 và PDF S3.
 * SPI2/FSPI tách khỏi SPI1 của flash/PSRAM, không xung đột DHT11 GPIO7.
 * DIO1 được ghi nhận để mở rộng, chưa cấu hình hoặc bật IRQ ở Task 5. */
#define BOARD_RADIO_CS_GPIO 10
#define BOARD_RADIO_MOSI_GPIO 11
#define BOARD_RADIO_SCK_GPIO 12
#define BOARD_RADIO_MISO_GPIO 13
#define BOARD_RADIO_DIO0_GPIO 4
#define BOARD_RADIO_RESET_GPIO 5
#define BOARD_RADIO_DIO1_GPIO 6

#endif
