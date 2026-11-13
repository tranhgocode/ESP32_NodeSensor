#include "platform/node_i2c.h"

#include <stddef.h>
#include <stdint.h>
#include "board_config.h"

#define SENSOR_I2C_GLITCH_IGNORE_COUNT 7U

static i2c_master_bus_handle_t bus;
static uint32_t users;

static node_result_t map_result(esp_err_t error)
{
    return error == ESP_OK ? NODE_OK :
        (error == ESP_ERR_TIMEOUT ? NODE_TIMEOUT : NODE_IO_ERROR);
}

node_result_t node_i2c_acquire(i2c_master_bus_handle_t *out_bus)
{
    if (out_bus == NULL)
    {
        return NODE_INVALID_ARG;
    }
    if (users == UINT32_MAX)
    {
        return NODE_OUT_OF_RANGE;
    }
    if (users == 0)
    {
        const i2c_master_bus_config_t config = {
            .i2c_port = -1,
            .sda_io_num = BOARD_SENSOR_I2C_SDA_GPIO,
            .scl_io_num = BOARD_SENSOR_I2C_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = SENSOR_I2C_GLITCH_IGNORE_COUNT,
            .flags.enable_internal_pullup = true,
        };
        i2c_master_bus_handle_t created = NULL;
        const node_result_t result = map_result(i2c_new_master_bus(&config, &created));
        if (result != NODE_OK)
        {
            return result;
        }
        bus = created;
    }
    ++users;
    *out_bus = bus;
    return NODE_OK;
}

node_result_t node_i2c_release(void)
{
    if (users == 0)
    {
        return NODE_INVALID_ARG;
    }
    if (users == 1)
    {
        const node_result_t result = map_result(i2c_del_master_bus(bus));
        if (result != NODE_OK)
        {
            return result;
        }
        bus = NULL;
    }
    --users;
    return NODE_OK;
}
