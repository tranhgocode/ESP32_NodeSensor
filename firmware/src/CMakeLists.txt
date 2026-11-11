idf_component_register(
    SRCS
        "main.c"
        "protocol/crc16.c"
        "protocol/node_packet.c"
        "protocol/node_payload.c"
        "app/node_state.c"
        "sensor/dht11.c"
    INCLUDE_DIRS "../include" "."
    PRIV_REQUIRES log freertos esp_driver_gpio esp-idf-lib__dht dernasherbrezon__sx127x
)
