#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* Người dùng xác nhận DATA GPIO7, nguồn 3.3V, module DHT11 ba chân có pull-up.
 * Schematic YD-ESP32-S3 V1.4: net GPIO7 đi tới U4 IO7 và J1 chân 7.
 * Đây là số GPIO, không phải chỉ số chân header của một board bất kỳ.
 * Không gán chân radio/cảm biến khác trước khi xác nhận dây nối riêng. */
#define BOARD_DHT11_GPIO 7

#endif
