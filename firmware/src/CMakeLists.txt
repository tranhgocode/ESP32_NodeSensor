idf_component_register(
    SRCS
        "main.c"
        "protocol/crc16.c"
        "protocol/node_packet.c"
        "protocol/node_payload.c"
        "app/node_state.c"
        "app/node_app.c"
        "platform/node_platform.c"
        "sensor/dht11.c"
        "sensor/battery.c"
        "sensor/hmi3301.c"
        "radio/node_radio.c"
        "power/node_power.c"
        "indicator/status_led.c"
    INCLUDE_DIRS "../include" "."
    PRIV_REQUIRES esp_hw_support log freertos esp_driver_gpio esp_driver_spi esp_driver_i2c esp_timer esp_rom esp_adc esp-idf-lib__dht dernasherbrezon__sx127x
)
