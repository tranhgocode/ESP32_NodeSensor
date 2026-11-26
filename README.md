# PM2.5 Monitoring and Edge AI Forecasting

## Overview

This project combines IoT air quality monitoring with on-device PM2.5 forecasting. The model uses 24 hourly observations of PM2.5, temperature, and relative humidity to predict PM2.5 concentrations for each of the next four hours. With hourly input, these forecasts can be used to estimate the PM2.5 component of the Air Quality Index (AQI).

The ESP32-S3 sensor node sends PM2.5, temperature, relative humidity, and battery voltage to a gateway over 433 MHz LoRa using a Ra-02 (SX1278) radio. The receiver can record received signal strength (RSSI) and signal-to-noise ratio (SNR) for each packet to monitor link quality.

The firmware targets the PlatformIO `esp32-s3-devkitc-1` board with 16 MB of flash and uses ESP-IDF 6.1.0. The exported model is bundled with the [firmware](firmware/). Training and evaluation are documented in the [Hanoi PM2.5 forecasting project](HN_PM25_forecasting/README.md).

## LoRa packet format

### Frame layout

```text
Field  | Type | Src | Dest | ID | Len | Payload   | Seq | CRC |
Bytes  |   1  |  1  |   1  |  1 |  1  | Len bytes |  2  |  2  |
```

- `Type` identifies the packet. `Src` and `Dest` are the source and destination addresses. Their defaults are `0x02` for the node and `0x00` for the gateway.
- `ID` and `Seq` identify the exchange. `Len` counts payload bytes, so the frame occupies `9 + Len` bytes.
- `CRC` is CRC-16/CCITT-FALSE over the bytes from `Type` through `Seq`, inclusive. All two-byte fields are transmitted most significant byte first.
- RSSI and SNR are receiver-side radio measurements. The packet carries PM2.5 concentrations for downstream AQI estimation.

### Packet types

```text
POLL   | 0x01 | Src | Dest | ID |  0  |              | Seq=0 | CRC |
DATA   | 0x02 | Src | Dest | ID | Len | Measurements | Seq   | CRC |
ACK    | 0x03 | Src | Dest | ID |  0  |              | Seq   | CRC |
ERROR  | 0x04 | Src | Dest | ID |  1  | Error code   | Seq   | CRC |
```

The blank payload cells show where `Seq` immediately follows `Len`.

| Packet  | Total size          | Purpose                                                                                                                                                                                                    |
| ------- | ------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `POLL`  | 9 bytes             | Polling request recognized by the frame codec. Measurements follow the node's own schedule.                                                                                                                |
| `DATA`  | 15, 17, or 25 bytes | The node sends sensor readings and includes forecasts when available.                                                                                                                                      |
| `ACK`   | 9 bytes             | The gateway swaps the source and destination and echoes the `ID` and `Seq` from the received `DATA` or `ERROR`. The node must validate the ACK before one second has elapsed from the end of transmission. |
| `ERROR` | 10 bytes            | The node sends one error code when it cannot produce a valid `DATA` frame.                                                                                                                                 |

### DATA payloads

Each field occupies two bytes. The gateway uses `Len` to select the layout.

```text
Len =  6 | Temp | RH | Battery |
Len =  8 | Temp | RH | Battery | PM2.5 |
Len = 16 | Temp | RH | Battery | PM2.5 | FC+1 | FC+2 | FC+3 | FC+4 |
```

| Field            | Encoding                                                                              |
| ---------------- | ------------------------------------------------------------------------------------- |
| `Temp`           | Signed 16-bit integer, temperature in °C multiplied by 10                             |
| `RH`             | Unsigned 16-bit integer, relative humidity in percent multiplied by 10                |
| `Battery`        | Unsigned 16-bit integer, battery voltage in mV                                        |
| `PM2.5`          | Unsigned 16-bit integer, measured PM2.5 concentration in µg/m³                        |
| `FC+1` to `FC+4` | Four unsigned 16-bit integers, predicted PM2.5 in µg/m³ for the next four model steps |

The node uses the 6-byte layout when HM3301 is disabled. With HM3301 enabled, it uses the 8-byte layout and extends it to 16 bytes when a forecast is available. A battery or measured PM2.5 value of `0xFFFF` indicates a missing reading. A PM2.5 value of zero is valid.

### ERROR payload

| Code   | Meaning                                               |
| ------ | ----------------------------------------------------- |
| `0x01` | Temperature and humidity sensor initialization failed |
| `0x02` | Temperature and humidity sensor read failed           |
| `0x03` | Temperature or humidity reading was out of range      |
| `0x04` | DATA encoding failed                                  |

See the [frame codec](firmware/src/protocol/node_packet.c), [payload encoder](firmware/src/protocol/node_payload.c), and [ACK validation](firmware/src/app/node_state.c).

## Features

- Measures temperature and relative humidity using either a DHT11 or an SHT30, selected at build time.
- Measures battery voltage through a voltage divider and a calibrated ADC.
- Supports optional PM2.5 measurements from an HM3301, with a warmup period before each reading.
- Runs a TensorFlow Lite Micro model on the ESP32-S3 to predict PM2.5 four steps ahead when forecasting is enabled and sufficient sensor history is available.
- Sends readings or an error over LoRa, protects packets with CRC-16, and waits up to one second after transmission for a matching gateway acknowledgment.
- Uses ESP32 Light-sleep and radio sleep between measurements on the standard build to reduce power use.

## Install firmware

Install Git and PlatformIO Core or the PlatformIO IDE extension for VS Code. If you use the extension, run the commands in its PlatformIO terminal.

1. Clone the repository and enter the firmware directory.

   ```powershell
   git clone https://github.com/tranhgocode/ESP32_NodeSensor.git
   cd ESP32_NodeSensor/firmware
   ```

2. Wire the sensors, battery voltage divider, and LoRa radio according to [`board_config.h`](firmware/include/board_config.h). Set `NODE_TEMP_HUM_SENSOR` in [`node_config.h`](firmware/include/node_config.h) to match the installed sensor. Set `NODE_HM3301_ENABLED` to `0` if the HM3301 is absent. The gateway must use the same LoRa settings and support the node's packet format to acknowledge packets.

3. Build the firmware. PlatformIO resolves the toolchain and component dependencies on the first build.

   ```powershell
   pio run -e esp32-s3-devkitc-1 -j 1
   ```

4. Connect the board over USB and list serial devices.

   ```powershell
   pio device list
   ```

5. Upload the firmware, then open the serial monitor. Replace `COMx` with the board's port. The firmware starts on the board after upload.

   ```powershell
   pio run -e esp32-s3-devkitc-1 -t upload --upload-port COMx
   pio device monitor -b 115200 --port COMx
   ```

The standard environment schedules measurements 60 seconds apart. To build the PM2.5 demonstration with 120-second measurement intervals, use `pio run -e esp32-s3-devkitc-1-forecast-demo -j 1` and use that environment for upload. The model was trained on hourly data. The firmware feeds it readings at the configured cadence, so each output represents a model step.

## Project structure

```text
ESP32_NodeSensor/
├── Docs/                          # Board schematics and component reference documents
├── HN_PM25_forecasting/           # PM2.5 model training, evaluation, and export
└── firmware/
    ├── platformio.ini             # Board target, build environments, and upload settings
    ├── sdkconfig.defaults         # ESP-IDF configuration defaults
    ├── include/
    │   ├── board_config.h         # GPIO assignments
    │   ├── node_config.h          # Sensor selection, measurement timing, and LoRa settings
    │   └── debug.h                # Log level control
    └── src/
        ├── main.c                 # Firmware startup and measurement loop
        ├── app/
        │   ├── node_app.c         # Measurement scheduling and LoRa exchange
        │   ├── node_state.c       # Packet counters and acknowledgment matching
        │   └── node_io.h          # Hardware interface used by the application
        ├── forecast/
        │   ├── pm25_history.c     # Builds the model input from sensor readings
        │   ├── pm25_forecast.cpp  # Runs PM2.5 inference on the ESP32-S3
        │   └── model/             # Bundled TFLite model and scaling coefficients
        ├── platform/
        │   ├── node_platform.c    # Connects the application to drivers and handles serial logs
        │   └── node_i2c.c         # Shared I²C bus for the SHT30 and HM3301
        ├── sensor/
        │   ├── select_sensor.c    # Selects the temperature and humidity sensor
        │   ├── dht11.c            # DHT11 temperature and humidity driver
        │   ├── sht30.c            # SHT30 temperature and humidity driver
        │   ├── hmi3301.c          # HM3301 PM2.5 driver
        │   └── battery.c          # Battery voltage measurement
        ├── protocol/
        │   ├── node_packet.c      # Packet encoding and decoding
        │   ├── node_payload.c     # Sensor and error payloads
        │   └── crc16.c            # CRC-16 calculation
        ├── radio/
        │   └── node_radio.c       # Ra-02 LoRa transmission, reception, and sleep
        ├── power/
        │   └── node_power.c       # ESP32 Light-sleep
        └── indicator/
            └── status_led.c       # Status LED control
```
