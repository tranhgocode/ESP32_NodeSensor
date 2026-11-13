#include "sensor/hmi3301.h"

#include <stddef.h>
#include "board_config.h"
#include "node_config.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "platform/node_i2c.h"

#define HM3301_ADDRESS 0x40U
#define HM3301_I2C_COMMAND 0x88U
#define HM3301_FRAME_SIZE 29U
#define HM3301_PM25_OFFSET 12U
#define HM3301_I2C_HZ 100000U
#define HM3301_I2C_TIMEOUT_MS 100
#define MICROSECONDS_PER_MILLISECOND INT64_C(1000)

/* Device thuộc driver, bus chỉ được mượn để cleanup không ảnh hưởng SHT30. */
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t device;
static bool enabled;
static bool set_configured;
static bool awake;
static int64_t ready_at_us;
static int64_t last_clock_us;

static node_result_t map_result(esp_err_t error)
{
    if (error == ESP_OK)
    {
        return NODE_OK;
    }
    return error == ESP_ERR_TIMEOUT ? NODE_TIMEOUT : NODE_IO_ERROR;
}

static node_result_t release_resources(void)
{
    if (device != NULL)
    {
        const esp_err_t error = i2c_master_bus_rm_device(device);
        if (error != ESP_OK)
        {
            return map_result(error);
        }
        device = NULL;
    }
    if (bus != NULL)
    {
        const node_result_t result = node_i2c_release();
        if (result != NODE_OK)
        {
            return result;
        }
        bus = NULL;
    }
    return NODE_OK;
}

node_result_t hm3301_sleep(void)
{
    awake = false;
    ready_at_us = 0;
    if (!set_configured)
    {
        return NODE_INIT_FAILED;
    }
    return map_result(gpio_set_level(BOARD_HM3301_SET_GPIO, 0));
}

node_result_t hm3301_init(bool requested_enabled)
{
    enabled = false;
    awake = false;
    ready_at_us = 0;
    /* Nạp mức LOW trước khi bật output để tránh làm quạt chạy lúc init. */
    node_result_t result = map_result(gpio_set_level(BOARD_HM3301_SET_GPIO, 0));
    if (result != NODE_OK)
    {
        return result;
    }
    result = release_resources();
    if (result != NODE_OK)
    {
        return result;
    }
    const gpio_config_t set_config = {
        .pin_bit_mask = UINT64_C(1) << BOARD_HM3301_SET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    result = map_result(gpio_config(&set_config));
    if (result != NODE_OK)
    {
        return result;
    }
    set_configured = true;
    /* Giữ mức SET hiện tại qua Light-sleep thay vì chuyển GPIO sang thả nổi. */
    result = map_result(gpio_sleep_sel_dis(BOARD_HM3301_SET_GPIO));
    if (result != NODE_OK)
    {
        return result;
    }
    if (!requested_enabled)
    {
        return NODE_OK;
    }
    result = node_i2c_acquire(&bus);
    if (result == NODE_OK)
    {
        const i2c_device_config_t device_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = HM3301_ADDRESS,
            .scl_speed_hz = HM3301_I2C_HZ,
        };
        result = map_result(i2c_master_bus_add_device(bus, &device_config, &device));
    }
    if (result != NODE_OK)
    {
        /* Giữ handle khi giải phóng lỗi để không làm mất dấu tài nguyên còn sở hữu. */
        const node_result_t cleanup = release_resources();
        return cleanup == NODE_OK ? result : cleanup;
    }
    enabled = true;
    return NODE_OK;
}

node_result_t hm3301_wake(uint32_t warmup_ms, int64_t *out_ready_at_us)
{
    if (out_ready_at_us == NULL || warmup_ms < NODE_HM3301_MIN_WARMUP_MS)
    {
        return NODE_INVALID_ARG;
    }
    if (!enabled || device == NULL || !set_configured)
    {
        return NODE_INIT_FAILED;
    }
    awake = false;
    const node_result_t result = map_result(gpio_set_level(BOARD_HM3301_SET_GPIO, 1));
    if (result != NODE_OK)
    {
        return result;
    }
    const int64_t now = esp_timer_get_time();
    const int64_t warmup_us = (int64_t)warmup_ms * MICROSECONDS_PER_MILLISECOND;
    if (now < 0 || now > INT64_MAX - warmup_us)
    {
        /* Chưa có deadline hợp lệ, caller sẽ đưa cảm biến về Sleep khi nhận lỗi. */
        return now < 0 ? NODE_IO_ERROR : NODE_OUT_OF_RANGE;
    }
    ready_at_us = now + warmup_us;
    last_clock_us = now;
    awake = true;
    *out_ready_at_us = ready_at_us;
    return NODE_OK;
}

node_result_t hm3301_read_pm25(uint16_t *out_ug_m3)
{
    if (out_ug_m3 == NULL)
    {
        return NODE_INVALID_ARG;
    }
    if (!enabled || device == NULL)
    {
        return NODE_INIT_FAILED;
    }
    if (!awake)
    {
        return NODE_NOT_READY;
    }
    const int64_t now = esp_timer_get_time();
    if (now < 0 || now < last_clock_us)
    {
        return NODE_IO_ERROR;
    }
    last_clock_us = now;
    if (now < ready_at_us)
    {
        return NODE_NOT_READY;
    }
    const uint8_t command = HM3301_I2C_COMMAND;
    node_result_t result = map_result(i2c_master_transmit(device, &command, sizeof(command),
                                                         HM3301_I2C_TIMEOUT_MS));
    if (result != NODE_OK)
    {
        return result;
    }
    uint8_t frame[HM3301_FRAME_SIZE];
    result = map_result(i2c_master_receive(device, frame, sizeof(frame), HM3301_I2C_TIMEOUT_MS));
    if (result != NODE_OK)
    {
        return result;
    }
    uint8_t checksum = 0;
    for (size_t i = 0; i < sizeof(frame) - 1; ++i)
    {
        checksum = (uint8_t)(checksum + frame[i]);
    }
    if (checksum != frame[sizeof(frame) - 1])
    {
        return NODE_CHECKSUM_ERROR;
    }
    const uint16_t pm25 = ((uint16_t)frame[HM3301_PM25_OFFSET] << 8) |
                         frame[HM3301_PM25_OFFSET + 1];
    if (pm25 == UINT16_MAX)
    {
        return NODE_OUT_OF_RANGE;
    }
    *out_ug_m3 = pm25;
    return NODE_OK;
}
