#include "sensor/sht30.h"

#include <stdbool.h>
#include <stddef.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "node_config.h"
#include "platform/node_i2c.h"

#define SHT30_I2C_HZ 100000U
#define SHT30_I2C_TIMEOUT_MS 100
#define SHT30_STOP_PERIODIC 0x3093U
#define SHT30_HEATER_OFF 0x3066U
#define SHT30_READ_STATUS 0xF32DU
#define SHT30_SINGLE_SHOT 0x2400U
#define SHT30_HEATER_STATUS_MASK 0x2000U
#define SHT30_STOP_WAIT_MS 1U
#define SHT30_COMMAND_WAIT_MS 10U
#define SHT30_MEASUREMENT_WAIT_MS 20U
#define SHT30_WORD_SIZE 2U
#define SHT30_STATUS_SIZE 3U
#define SHT30_FRAME_SIZE 6U
#define SHT30_CRC_POLYNOMIAL 0x31U
#define SHT30_CRC_INITIAL 0xFFU
#define SHT30_RAW_MAX INT64_C(65535)
#define SHT30_TEMPERATURE_OFFSET_X10 INT64_C(-450)
#define SHT30_TEMPERATURE_SCALE_X10 INT64_C(1750)
#define SHT30_HUMIDITY_SCALE_X10 INT64_C(1000)
#define SHT30_TEMPERATURE_MIN_X10 (-400)
#define SHT30_TEMPERATURE_MAX_X10 1250
#define MICROSECONDS_PER_MILLISECOND INT64_C(1000)

static const char *TAG = "sht30";
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t device;
static bool initialized;

static node_result_t map_result(esp_err_t error)
{
    return error == ESP_OK ? NODE_OK :
        (error == ESP_ERR_TIMEOUT ? NODE_TIMEOUT : NODE_IO_ERROR);
}

static node_result_t release_resources(void)
{
    if (device != NULL)
    {
        const node_result_t result = map_result(i2c_master_bus_rm_device(device));
        if (result != NODE_OK)
        {
            return result;
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

static node_result_t wait_ms(uint32_t duration_ms)
{
    int64_t now = esp_timer_get_time();
    const int64_t duration_us = (int64_t)duration_ms * MICROSECONDS_PER_MILLISECOND;
    if (now < 0)
    {
        return NODE_IO_ERROR;
    }
    if (now > INT64_MAX - duration_us)
    {
        return NODE_OUT_OF_RANGE;
    }
    const int64_t deadline = now + duration_us;
    while (now < deadline)
    {
        const uint32_t remaining_ms = (uint32_t)((deadline - now +
            MICROSECONDS_PER_MILLISECOND - 1) / MICROSECONDS_PER_MILLISECOND);
        const TickType_t ticks = pdMS_TO_TICKS(remaining_ms);
        /* Tick có thể kết thúc sớm, clock thực quyết định lúc được đọc I2C. */
        vTaskDelay(ticks == 0 ? 1 : ticks);
        const int64_t after = esp_timer_get_time();
        if (after <= now)
        {
            return NODE_IO_ERROR;
        }
        now = after;
    }
    return NODE_OK;
}

static uint8_t crc8_word(const uint8_t data[SHT30_WORD_SIZE])
{
    uint8_t crc = SHT30_CRC_INITIAL;
    for (size_t i = 0; i < SHT30_WORD_SIZE; ++i)
    {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
        {
            crc = (uint8_t)((crc << 1) ^ ((crc & 0x80U) ? SHT30_CRC_POLYNOMIAL : 0U));
        }
    }
    return crc;
}

static node_result_t send_command(uint16_t command)
{
    const uint8_t bytes[] = {(uint8_t)(command >> 8), (uint8_t)command};
    return map_result(i2c_master_transmit(device, bytes, sizeof(bytes), SHT30_I2C_TIMEOUT_MS));
}

static node_result_t fail_init(node_result_t cause)
{
    const node_result_t cleanup = release_resources();
    ESP_LOGE(TAG, "Init failed result=%d cleanup=%d", (int)cause, (int)cleanup);
    return NODE_INIT_FAILED;
}

node_result_t sht30_init(void)
{
    initialized = false;
    node_result_t result = release_resources();
    if (result != NODE_OK)
    {
        ESP_LOGE(TAG, "Previous resource cleanup failed result=%d", (int)result);
        return NODE_INIT_FAILED;
    }
    result = node_i2c_acquire(&bus);
    if (result != NODE_OK)
    {
        return fail_init(result);
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = NODE_SHT30_I2C_ADDRESS,
        .scl_speed_hz = SHT30_I2C_HZ,
    };
    result = map_result(i2c_master_bus_add_device(bus, &config, &device));
    if (result == NODE_OK)
    {
        result = send_command(SHT30_STOP_PERIODIC);
    }
    if (result == NODE_OK)
    {
        result = wait_ms(SHT30_STOP_WAIT_MS);
    }
    if (result == NODE_OK)
    {
        result = send_command(SHT30_HEATER_OFF);
    }
    if (result == NODE_OK)
    {
        /* Chờ xử lý lệnh như driver Sensirion trước khi kiểm chứng trạng thái. */
        result = wait_ms(SHT30_COMMAND_WAIT_MS);
    }
    if (result == NODE_OK)
    {
        result = send_command(SHT30_READ_STATUS);
    }
    if (result == NODE_OK)
    {
        result = wait_ms(SHT30_COMMAND_WAIT_MS);
    }
    uint8_t status[SHT30_STATUS_SIZE];
    if (result == NODE_OK)
    {
        result = map_result(i2c_master_receive(device, status, sizeof(status), SHT30_I2C_TIMEOUT_MS));
    }
    if (result == NODE_OK && crc8_word(status) != status[SHT30_WORD_SIZE])
    {
        result = NODE_CHECKSUM_ERROR;
    }
    if (result == NODE_OK &&
        ((((uint16_t)status[0] << 8) | status[1]) & SHT30_HEATER_STATUS_MASK) != 0)
    {
        result = NODE_IO_ERROR;
    }
    if (result != NODE_OK)
    {
        return fail_init(result);
    }
    initialized = true;
    return NODE_OK;
}

static int32_t rounded_x10(int64_t numerator)
{
    /* Làm tròn sau khi trừ offset để nhiệt độ âm gần 0 không bị lệch dấu. */
    return (int32_t)(numerator < 0 ?
        -((-numerator + SHT30_RAW_MAX / 2) / SHT30_RAW_MAX) :
        (numerator + SHT30_RAW_MAX / 2) / SHT30_RAW_MAX);
}

node_result_t sht30_read(node_measurement_t *out)
{
    if (out == NULL)
    {
        return NODE_INVALID_ARG;
    }
    if (!initialized)
    {
        return NODE_INIT_FAILED;
    }
    node_result_t result = send_command(SHT30_SINGLE_SHOT);
    if (result == NODE_OK)
    {
        result = wait_ms(SHT30_MEASUREMENT_WAIT_MS);
    }
    uint8_t frame[SHT30_FRAME_SIZE];
    if (result == NODE_OK)
    {
        result = map_result(i2c_master_receive(device, frame, sizeof(frame), SHT30_I2C_TIMEOUT_MS));
    }
    if (result != NODE_OK)
    {
        return result;
    }
    if (crc8_word(frame) != frame[2] || crc8_word(frame + 3) != frame[5])
    {
        return NODE_CHECKSUM_ERROR;
    }
    const uint16_t raw_temperature = ((uint16_t)frame[0] << 8) | frame[1];
    const uint16_t raw_humidity = ((uint16_t)frame[3] << 8) | frame[4];
    const int32_t temperature = rounded_x10(SHT30_TEMPERATURE_OFFSET_X10 * SHT30_RAW_MAX +
        SHT30_TEMPERATURE_SCALE_X10 * raw_temperature);
    const int32_t humidity = rounded_x10(SHT30_HUMIDITY_SCALE_X10 * raw_humidity);
    if (temperature < SHT30_TEMPERATURE_MIN_X10 || temperature > SHT30_TEMPERATURE_MAX_X10 ||
        humidity < 0 || humidity > SHT30_HUMIDITY_SCALE_X10)
    {
        return NODE_OUT_OF_RANGE;
    }
    *out = (node_measurement_t){
        .temperature_x10 = (int16_t)temperature,
        .humidity_x10 = (uint16_t)humidity,
    };
    return NODE_OK;
}
