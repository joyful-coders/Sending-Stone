# Nicla Voice Safety Recorder

Firmware for the [Arduino Nicla Voice](https://docs.arduino.cc/hardware/nicla-voice/) (ABX00061), worn as a watch to capture evidence when the wearer is attacked. It records audio and motion continuously into a rolling buffer on the board's flash; when a trigger fires, it saves the **~10 s before and 50 s after** as an event, alerts a connected phone/PC immediately, and hands the event over BLE once it's done.

## Features

- **Rolling buffer:** 16 kHz audio (IMA-ADPCM, ~8 KB/s) and 50 Hz motion recorded continuously into 2 s segment files on the 16 MB external flash, keeping the last ~10-12 s (`recorder_config`)
- **Triggers** (`trigger_config`):
  - **Keyword** recognized by the NDP120 (currently the stock model's "Alexa", as a stand-in)
  - **Jolt**: total acceleration >= 4 g or rotation >= 700 dps (starting guesses, tune with data)
  - **Manual**: the client's TRIGGER command (and later a button)
- **Events:** a trigger keeps the pre-trigger buffer and records 50 s more; further triggers extend it (up to 3 min). Up to 16 events are stored, survive power loss, and wait for a client
- **BLE store-and-forward:** an immediate EVENT_STARTED alert, then a resumable chunked download, see [BLE_PROTOCOL.md](BLE_PROTOCOL.md). The Python client in `../hardware-ble-test/` saves each event as WAV + CSV + JSON
- **BMI270** at ±16 g / ±2000 dps so strikes aren't clipped
- Normal builds carry no debug code: if a module fails to start, the device reboots after 10 s to retry (`main_config::kFailureRebootDelayMs`)
- Debug builds (see [Debug builds](#debug-builds)) add status patterns on the RGB LED and queued, per-module serial logging (`[NDP]`, `[IMU]`, `[Audio]`, `[Rec]`, `[Trig]`, `[BLE]`, `[LED]`)

Not done yet: filtering out everyday motion (running, sports) and loud places, a raised-voice trigger, and a custom keyword model. These need real recorded data to tune against; the saved events (`motion.csv`, `audio.wav`) are that data.

## Hardware

- Arduino Nicla Voice (ABX00061). No external sensors or wiring.
- Power: USB, VIN (5 V), or a single-cell LiPo-range supply (about 3.0-4.2 V) on the VBAT pads. The board regulates internally from any of these. The NTC pad is a thermistor sense input, not a power pin: leave it unconnected.

The IMU and microphone are not wired to the nRF52832 directly. They sit behind the board's **Syntiant NDP120** neural processor, which must be running its firmware before any of them respond. That firmware loads at boot from the board's external flash (see [NDP firmware](#ndp-firmware)).

## Project layout

- `hardware.ino` - entry point: brings each module up in order, then ticks them all from `loop()`
- `src/configs.h` - every tunable setting, grouped by module
- `src/logger.h` / `logger.cpp` - non-allocating debug log queue (debug builds only)
- `src/module_utils.h` - shared module helpers: idle-until-started threads (`makeIdleThread`, `runIfDue`) and `LatestReading<T>`
- `src/led/` - device state tracking, plus the RGB status LED in debug builds, driven by the `BLINK_STATES` table in `led_handler.h`
- `src/ndp/` - loads the NDP120 firmware packages and reports keyword matches
- `src/imu/` - samples the BMI270 at 50 Hz and hands each raw sample to its handlers
- `src/audio/` - extracts every 16 kHz audio chunk from the NDP and tracks loudness
- `src/recorder/` - the rolling buffer and event storage on flash (`event_format.h` is an event's byte format, `adpcm.h` the audio encoder)
- `src/trigger/` - keyword, jolt, and manual triggers
- `src/ble/` - the GATT service and event transfer (`ble_protocol.h` is the command/message format)
- `sketch.yaml` - Arduino CLI profile pinning the board, core, and library versions
- `../hardware-ble-test/` - Python client: listens for events, downloads and decodes them into `output/`

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

# Watch the debug logs (debug builds only, see below)
arduino-cli monitor -p COM5 -c baudrate=115200
```

In VS Code the same steps are tasks (`Ctrl+Shift+P` → **Tasks: Run Task**): **Nicla: Build**, **Nicla: Upload**, **Nicla: Monitor**, **Nicla: List Ports**.

## Debug builds

Normal builds leave out the status LED and serial logging entirely. To get them, build with `NICLA_DEBUG=1`, using the VS Code tasks **Nicla: Build (debug)** / **Nicla: Upload (debug)**, or:

```bash
arduino-cli compile --profile nicla_voice --build-path build/debug --build-property "compiler.cpp.extra_flags=-DNICLA_DEBUG=1" --upload -p COM5
```

A debug build also stays in a failed state (showing it on the LED and serial log) instead of rebooting to retry.

If a sketch locks the board up so it no longer appears as a port, **double-tap the reset button** right after plugging it in to enter the bootloader, then flash again.

## NDP firmware

At boot, `startNDPModule()` loads three Syntiant packages from the board's external flash (names in `ndp_config`, `src/configs.h`):

- `mcu_fw_120_v91.synpkg`
- `dsp_firmware_v91.synpkg`
- `alexa_334_NDP120_B0_v11_v91.synpkg`

These normally ship preloaded, but not always. If they are missing, a debug build shows the **NDP failure** LED pattern and the serial log prints `Error opening: mcu_fw_120_v91.synpkg`. To restore them:

1. Download and unzip Arduino's [nicla_voice_uploader_and_firmwares.zip](https://docs.arduino.cc/eb3909ae150b93f6804ff42901ce0301/nicla_voice_uploader_and_firmwares.zip) into `Downloads\nicla_voice_uploader_and_firmwares\` (it contains the three packages and `syntiant-uploader-win.exe`).
2. Close every serial monitor, then run the VS Code task **Nicla: Flash NDP Provisioner**. It flashes `tools/ndp_provision`, a verbose version of the core's `Syntiant_upload_fw_ymodem` example that also keeps the NDP120 off the SPI bus it shares with the flash. Its LED shows green when the flash is mounted, red when it isn't.
3. Run **Nicla: Upload NDP Firmware** (`tools/upload-ndp-firmware.ps1`). It formats the flash only if it won't mount, sends the three packages over YMODEM with its own built-in sender (Arduino's `syntiant-uploader` fails against this board with "Didn't get a nak when expected"), then verifies each file's sha256 on the board.
4. Run **Nicla: Upload (slow)** to put this firmware back.

If an OpenOCD task fails with `Error connecting DP: cannot read IDR`, unplug the board for a few seconds and retry.

## Configuration

Everything lives in `src/configs.h`, grouped by namespace:

- `imu_config::kAccelRangeG` / `kGyroRangeDps` - BMI270 ranges (default ±8 g, ±1000 °/s)
- `recorder_config` - segment length, pre/post-trigger windows (10 s / 50 s), maximum event length, stored event count
- `trigger_config` - which triggers are on, and the jolt thresholds and cooldown
- `imu_config` - BMI270 ranges and the 50 Hz sample interval
- `ble_config::kDeviceName` and the service/characteristic UUIDs
- `main_config::kFailureRebootDelayMs` - how long a normal build waits before rebooting after a failed start
- `led_config::kIntensity` - LED brightness, 1 to 8 (debug builds)
- `debug_config` - the per-module `kEnableXLogging` flags (debug builds)

## Status LED (debug builds)

| Pattern | Meaning |
|---|---|
| Blue (steady while modules start) | Starting up (NDP firmware load takes a few seconds); stuck on blue means setup hung |
| Short green blip every 2 s | Running normally |
| Slow magenta blink | NDP120 firmware failed to load, see [NDP firmware](#ndp-firmware) |
| Two red blinks | BMI270 IMU failed to initialize |
| Slow yellow blink | Microphone failed to start |
| Slow cyan blink | External flash couldn't be mounted for recording |
| Long then short white blink | BLE failed to start |

On any failure the device stays in that pattern and does not advertise. The serial log says which step failed.
