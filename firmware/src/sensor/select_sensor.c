#include "sensor/select_sensor.h"

#include "node_config.h"
#if NODE_TEMP_HUM_SENSOR == NODE_TEMP_HUM_SENSOR_DHT11
#include "sensor/dht11.h"
#else
#include "sensor/sht30.h"
#endif

node_result_t node_sensor_init(void)
{
#if NODE_TEMP_HUM_SENSOR == NODE_TEMP_HUM_SENSOR_DHT11
    return dht11_init();
#else
    return sht30_init();
#endif
}

node_result_t node_sensor_read(node_measurement_t *out)
{
#if NODE_TEMP_HUM_SENSOR == NODE_TEMP_HUM_SENSOR_DHT11
    return dht11_read(out);
#else
    return sht30_read(out);
#endif
}

const char *node_sensor_name(void)
{
#if NODE_TEMP_HUM_SENSOR == NODE_TEMP_HUM_SENSOR_DHT11
    return "DHT11";
#else
    return "SHT30";
#endif
}
