# Nicla Voice Sensor Streamer

Firmware for the [Arduino Nicla Voice](https://docs.arduino.cc/hardware/nicla-voice/) (ABX00061) that reads every onboard sensor and streams the readings over Bluetooth Low Energy as one compact packet, for a client app to log, graph, or feed into further processing.

## Features

- Streams all onboard sensors in a single 30-byte BLE notification at 20 Hz (rate: `ble_config::kNotifyIntervalMs`, `src/configs.h`). Wire format and decoding guide: [BLE_PROTOCOL.md](BLE_PROTOCOL.md)
- **BMI270** 6-axis IMU: acceleration (m/s²), angular rate (rad/s), and die temperature, with configurable ranges (`imu_config`)
- **BMM150** magnetometer: factory-trim compensated field in µT
- **IM69D130** microphone: loudness as the RMS of each audio chunk from the NDP120's 16 kHz pipeline
- **BQ25120A** PMIC: battery voltage, charge percentage, and on-battery/charging status
- Status patterns on the RGB LED for setup, each module's failure, and idle, visible without a serial monitor
- Queued, per-module debug logging (`[NDP]`, `[IMU]`, `[Mag]`, `[Sound]`, `[Battery]`, `[BLE]`, `[LED]`) flushed to Serial once a second (toggle: `debug_config::kEnableVerboseLogging`)

## Hardware

- Arduino Nicla Voice (ABX00061). No external sensors or wiring.
- Optional: single-cell 3.7 V LiPo on the J4 battery connector for untethered use.

The IMU, magnetometer, and microphone are not wired to the nRF52832 directly. They sit behind the board's **Syntiant NDP120** neural processor, which must be running its firmware before any of them respond. That firmware loads at boot from the board's external flash (see [NDP firmware](#ndp-firmware)).

## Project layout

- `hardware.ino` - entry point: brings each module up in order, then ticks them all from `loop()`
- `src/configs.h` - every tunable setting, grouped by module
- `src/logger.h` / `logger.cpp` - non-allocating debug log queue
- `src/module_utils.h` - shared module helpers: idle-until-started threads (`makeIdleThread`, `runIfDue`) and `LatestReading<T>`
- `src/led/` - RGB status LED, driven by the `BLINK_STATES` table in `led_handler.h`
- `src/ndp/` - loads the NDP120 firmware packages
- `src/imu/`, `src/mag/`, `src/sound/`, `src/battery/` - one module per sensor, each sampling on its own cooperative thread and exposing a `getLatest…Reading()` accessor
- `src/ble/` - the GATT service and packet encoding (`ble_protocol.h` is the wire format)
- `sketch.yaml` - Arduino CLI profile pinning the board, core, and library versions
- `../hardware-ble-test/` - Python client for testing the BLE stream from a PC

## Requirements

- [Arduino CLI](https://arduino.github.io/arduino-cli/) (also bundled with Arduino IDE 2 under `resources/app/lib/backend/resources/arduino-cli.exe`)
- The board core and libraries, pinned in `sketch.yaml`:
  ```bash
  arduino-cli core install arduino:mbed_nicla@4.6.0
  arduino-cli lib install ArduinoBLE@2.0.1 ArduinoThread@2.1.1
  ```

PlatformIO has no Nicla Voice board definition, so this project builds with Arduino CLI (or the Arduino IDE) only.

## Build & Flash

Run from this `hardware/` folder:

```bash
# Build
arduino-cli compile --profile nicla_voice

# Find the board's port
arduino-cli board list

# Flash (replace COM5 with the port from above)
arduino-cli upload --profile nicla_voice -p COM5

# Watch the debug logs
arduino-cli monitor -p COM5 -c baudrate=115200
```

If a sketch locks the board up so it no longer appears as a port, **double-tap the reset button** right after plugging it in to enter the bootloader, then flash again.

## NDP firmware

At boot, `startNDPModule()` loads three Syntiant packages from the board's external flash (names in `ndp_config`, `src/configs.h`):

- `mcu_fw_120_v91.synpkg`
- `dsp_firmware_v91.synpkg`
- `alexa_334_NDP120_B0_v11_v91.synpkg`

These normally ship preloaded, but not always. If they are missing, the LED shows the **NDP failure** pattern and the serial log prints `Error opening: mcu_fw_120_v91.synpkg`. To restore them:

1. Download and unzip Arduino's [nicla_voice_uploader_and_firmwares.zip](https://docs.arduino.cc/eb3909ae150b93f6804ff42901ce0301/nicla_voice_uploader_and_firmwares.zip) into `Downloads\nicla_voice_uploader_and_firmwares\` (it contains the three packages and `syntiant-uploader-win.exe`).
2. Close every serial monitor, then run the VS Code task **Nicla: Flash NDP Provisioner**. It flashes `tools/ndp_provision`, a verbose version of the core's `Syntiant_upload_fw_ymodem` example that also keeps the NDP120 off the SPI bus it shares with the flash. Its LED shows green when the flash is mounted, red when it isn't.
3. Run **Nicla: Upload NDP Firmware** (`tools/upload-ndp-firmware.ps1`). It formats the flash only if it won't mount, sends the three packages over YMODEM with its own built-in sender (Arduino's `syntiant-uploader` fails against this board with "Didn't get a nak when expected"), then verifies each file's sha256 on the board.
4. Run **Nicla: Upload (slow)** to put this firmware back.

If an OpenOCD task fails with `Error connecting DP: cannot read IDR`, unplug the board for a few seconds and retry.

## Configuration

Everything lives in `src/configs.h`, grouped by namespace:

- `imu_config::kAccelRangeG` / `kGyroRangeDps` - BMI270 ranges (default ±8 g, ±1000 °/s)
- `*_config::kThreadRefreshIntervalMs` - each sensor's sample interval (default 50 ms; battery 5 s)
- `ble_config::kNotifyIntervalMs` - how often a packet is sent (defaults to the IMU interval)
- `ble_config::kDeviceName` and the service/characteristic UUIDs
- `led_config::kIntensity` - LED brightness, 1 to 8
- `debug_config::kEnableVerboseLogging` and the per-module `kEnableXLogging` flags

## Status LED

| Pattern | Meaning |
|---|---|
| Blue blink | Starting up (NDP firmware load takes a few seconds) |
| Short green blip every 2 s | Running normally |
| Slow magenta blink | NDP120 firmware failed to load, see [NDP firmware](#ndp-firmware) |
| Two red blinks | BMI270 IMU failed to initialize |
| Three red blinks | BMM150 magnetometer failed to initialize |
| Slow yellow blink | Microphone failed to start |
| Long then short blue blink | BLE failed to start |

On any failure the device stays in that pattern and does not advertise. The serial log says which step failed.
