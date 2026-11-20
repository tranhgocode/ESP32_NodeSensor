idf_component_register(
    SRCS
        "main.c"
        "protocol/crc16.c"
        "protocol/node_packet.c"
        "protocol/node_payload.c"
        "app/node_state.c"
        "app/node_app.c"
        "platform/node_platform.c"
        "platform/node_i2c.c"
        "forecast/pm25_history.c"
        "forecast/pm25_forecast.cpp"
        "forecast/model/pm25_model.S"
        "sensor/dht11.c"
        "sensor/sht30.c"
        "sensor/select_sensor.c"
        "sensor/battery.c"
        "sensor/hmi3301.c"
        "radio/node_radio.c"
        "power/node_power.c"
        "indicator/status_led.c"
    INCLUDE_DIRS "../include" "."
    PRIV_REQUIRES esp_hw_support log freertos esp_driver_gpio esp_driver_spi esp_driver_i2c esp_timer esp_rom esp_adc esp-idf-lib__dht dernasherbrezon__sx127x espressif__esp-tflite-micro
)

set(pm25_model "${CMAKE_CURRENT_SOURCE_DIR}/forecast/model/model.tflite")
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    "$<$<COMPILE_LANGUAGE:ASM>:PM25_MODEL_PATH=\"${pm25_model}\">")
set_source_files_properties("forecast/model/pm25_model.S" PROPERTIES
    OBJECT_DEPENDS "${pm25_model}")
