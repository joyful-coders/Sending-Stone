/**
 * @file configs.h
 *
 * @brief Build-time configuration constants used across the application.
 *
 * @details
 * Every tunable value in the firmware lives here, grouped by the module
 * that consumes it. Nothing in this file allocates or runs logic, it is
 * read-only, compile-time configuration shared by every other module via
 * `#include "configs.h"`.
 *
 */

#pragma once

#include <stddef.h>
#include <cstdint>
#include <Arduino.h>

/**
 * @brief 1 for a debug build (status LED + serial logging), 0 for a normal build.
 *
 * @details
 * Normally left at 0. The VS Code tasks "Nicla: Build (debug)" and "Nicla:
 * Upload (debug)" set it to 1 on the command line with
 * `--build-property compiler.cpp.extra_flags=-DNICLA_DEBUG=1`. In a normal
 * build the LED and logger code is compiled out entirely.
 *
 */
#ifndef NICLA_DEBUG
#define NICLA_DEBUG 0
#endif

namespace main_config {
/** @brief Delay, in milliseconds, between successive loop() iterations. */
constexpr uint8_t kRefreshIntervalMs = 2;

/**
 * @brief How long, in milliseconds, a normal build waits after a module fails to start before rebooting to retry.
 *
 * @details
 * Debug builds instead stay in the failed state, showing it on the status LED
 * and serial log, so it can be diagnosed.
 *
 */
constexpr unsigned long kFailureRebootDelayMs = 1UL * 10UL * 1000UL; // 10 seconds

} // namespace main_config

namespace led_config {
/**
 * @brief Brightness of the onboard RGB LED (debug builds only).
 *
 * @par Options
 * 1 (dimmest) to 8 (full).
 *
 */
constexpr uint8_t kIntensity = 4;

/** @brief Interval, in milliseconds, between status LED thread ticks. */
constexpr uint8_t kThreadRefreshIntervalMs = 20;

} // namespace led_config

namespace ndp_config {
/**
 * @brief Syntiant NDP120 packages loaded from the board's external flash at boot, in order.
 *
 * @details
 * The MCU firmware and DSP firmware must come first. The model package is
 * required for the NDP's audio pipeline (and so the microphone) to run, and
 * provides the keyword trigger. See README.md if they need uploading.
 *
 */
constexpr char kMcuFirmware[] = "mcu_fw_120_v91.synpkg";
/** @brief DSP firmware package, see @c kMcuFirmware. */
constexpr char kDspFirmware[] = "dsp_firmware_v91.synpkg";
/** @brief Neural network model package, see @c kMcuFirmware. Its keywords are triggers. */
constexpr char kModel[] = "alexa_334_NDP120_B0_v11_v91.synpkg";

} // namespace ndp_config

namespace imu_config {
/**
 * @brief BMI270 accelerometer full-scale range, in g.
 *
 * @details
 * Wide by default so a strike or fall is captured without clipping.
 *
 * @par Options
 * 2, 4, 8, or 16.
 *
 */
constexpr uint8_t kAccelRangeG = 16;

/**
 * @brief BMI270 gyroscope full-scale range, in degrees per second.
 *
 * @par Options
 * 125, 250, 500, 1000, or 2000.
 *
 */
constexpr uint16_t kGyroRangeDps = 2000;

/** @brief Number of attempts made when the BMI270 fails to initialize. */
constexpr uint8_t kInitAttempts = 3;

/** @brief Interval, in milliseconds, between BMI270 samples (IMU thread ticks). 20 ms = 50 Hz. */
constexpr uint8_t kThreadRefreshIntervalMs = 20;

} // namespace imu_config

namespace audio_config {
/**
 * @brief Interval, in milliseconds, between checks for a new audio chunk from the NDP.
 *
 * @details
 * Each NDP chunk is ~16 ms of 16 kHz audio. Checking at least twice per chunk
 * keeps up with the stream; a chunk that hasn't changed (same NDP counter) is
 * skipped, and missed chunks are recorded as gaps.
 *
 */
constexpr uint8_t kExtractIntervalMs = 6;

/** @brief Maximum chunks extracted per check, so a backlog (e.g. after a flash erase) drains without starving the loop. */
constexpr uint8_t kMaxChunksPerTick = 4;

} // namespace audio_config

namespace recorder_config {
/** @brief Length, in milliseconds, of one rolling-buffer segment file. */
constexpr unsigned long kSegmentMs = 1UL * 2UL * 1000UL; // 2 seconds

/** @brief Audio and motion kept from before a trigger, in milliseconds. The ring holds enough segments to cover it. */
constexpr unsigned long kPreTriggerMs = 1UL * 10UL * 1000UL; // 10 seconds

/** @brief Recording kept after the (latest) trigger, in milliseconds. */
constexpr unsigned long kPostTriggerMs = 1UL * 50UL * 1000UL; // 50 seconds

/** @brief Longest a single event may run, in milliseconds, even if triggers keep extending it. */
constexpr unsigned long kMaxEventMs = 3UL * 60UL * 1000UL; // 3 minutes

/**
 * @brief Maximum events kept on the flash.
 *
 * @details
 * Each 60 s event is ~0.55 MB, so 16 events use ~9 MB of the 16 MB flash,
 * leaving room for the NDP firmware and the rolling buffer. When full, the
 * oldest event is deleted to make room.
 *
 */
constexpr uint8_t kMaxEvents = 16;

/** @brief Motion samples buffered per motion record. 10 samples = 0.2 s at 50 Hz (kept small: RAM is tight). */
constexpr uint8_t kMotionSamplesPerRecord = 10;

/** @brief Name the event filesystem is mounted under (must differ from the NDP library's "fs"). */
constexpr char kMountName[] = "rec";

/**
 * @brief Bytes the filesystem writes to flash at a time (one flash page).
 *
 * @details
 * Every flash write has a fixed cost (waiting for the chip, then reading the
 * data back to check it). At the core's default of 64 bytes, recording took
 * ~36 ms per 24 ms audio chunk and audio fell behind. Larger writes cost RAM:
 * the filesystem and each open file keep a cache this size.
 *
 */
constexpr uint16_t kFlashProgramBytes = 256;

} // namespace recorder_config

namespace trigger_config {
/** @brief When true, any keyword the NDP model recognizes starts an event. */
constexpr bool kEnableKeyword = true;
/** @brief Interval, in milliseconds, between checks for an NDP keyword match. */
constexpr uint8_t kKeywordPollMs = 50;

/** @brief When true, a sudden jolt of movement starts an event. */
constexpr bool kEnableJolt = true;
/**
 * @brief Total acceleration, in g, that counts as a jolt (gravity alone is 1 g).
 *
 * @note
 * A starting guess: running peaks around 2-4 g at the wrist. Tune it with
 * recorded data before relying on it.
 *
 */
constexpr float kJoltAccelG = 4.0f;
/** @brief Total angular rate, in degrees per second, that counts as a jolt. Also a starting guess. */
constexpr float kJoltGyroDps = 700.0f;
/** @brief Minimum time, in milliseconds, between two jolt triggers, so one impact doesn't trigger repeatedly. */
constexpr unsigned long kJoltCooldownMs = 1UL * 2UL * 1000UL; // 2 seconds

} // namespace trigger_config

namespace ble_config {
/** @brief Device name shown to scanning clients. */
constexpr char kDeviceName[] = "Nicla-Sensors";

/** @brief UUID of the device's service, advertised so clients can filter scans by it. */
constexpr char kServiceUuid[] = "db118277-ac3c-4312-9c3f-8f0f77e70acc";
/** @brief UUID of the protocol version characteristic (READ), carrying @c BLE_PROTOCOL_VERSION. */
constexpr char kVersionCharUuid[] = "48e4c40b-6e01-49f1-b99b-0e6e9bd77a16";
/** @brief UUID of the control characteristic (WRITE): commands from the client. */
constexpr char kControlCharUuid[] = "5eb136f7-3b60-4ea6-a592-3b9778aed258";
/** @brief UUID of the events characteristic (NOTIFY): event and reply messages to the client. */
constexpr char kEventsCharUuid[] = "6c33adb5-05dd-4186-bc22-624ed92abb28";
/** @brief UUID of the data characteristic (NOTIFY): event file chunks during a transfer. */
constexpr char kDataCharUuid[] = "40d3f957-dded-4b7d-9eb2-f11db97dda09";

/** @brief Largest data notification, in bytes. Clients ask for less if their MTU is smaller. */
constexpr uint16_t kMaxDataNotifyBytes = 240;

/** @brief Data chunks sent per loop pass during a transfer. Each can block until the radio frees a buffer. */
constexpr uint8_t kChunksPerLoop = 2;

} // namespace ble_config

namespace debug_config {
/** @brief Whether this is a debug build, see @c NICLA_DEBUG. */
constexpr bool kDebugBuild = NICLA_DEBUG != 0;

/** @brief When true, the onboard status LED reflects device state. Debug builds only. */
constexpr bool kEnableStatusLight = kDebugBuild;

/** @brief Master switch for all verbose debug logging. Debug builds only. */
constexpr bool kEnableVerboseLogging = kDebugBuild;
/** @brief Maximum time, in milliseconds, setup() waits for a serial monitor to connect. */
constexpr unsigned long kSerialWaitTimeoutMs = 1UL * 3UL * 1000UL; // 3 seconds
/** @brief Interval, in milliseconds, between flushes of the log queue. */
constexpr unsigned long kLoopLogDelay = 1UL * 1UL * 1000UL; // 1 second

/** @brief Enables setup and memory log messages from hardware.ino. */
constexpr bool kEnableMainLogging = true && kEnableVerboseLogging;
/** @brief Prefix prepended to setup and memory log messages. */
constexpr const char* kMainPrefix = "[Main]";

/** @brief Enables status LED log messages. */
constexpr bool kEnableLEDLogging = true && kEnableVerboseLogging;
/** @brief Prefix prepended to status LED log messages. */
constexpr const char* kLEDPrefix = "[LED]";

/** @brief Enables NDP module log messages. */
constexpr bool kEnableNDPLogging = true && kEnableVerboseLogging;
/** @brief Prefix prepended to NDP module log messages. */
constexpr const char* kNDPPrefix = "[NDP]";

/** @brief Enables IMU module log messages. */
constexpr bool kEnableIMULogging = true && kEnableVerboseLogging;
/** @brief Prefix prepended to IMU module log messages. */
constexpr const char* kIMUPrefix = "[IMU]";
/** @brief Interval, in milliseconds, between periodic IMU reading logs. */
constexpr unsigned long kIMULoopDelay = 1UL * 2UL * 1000UL; // 2 seconds

/** @brief Enables audio module log messages. */
constexpr bool kEnableAudioLogging = true && kEnableVerboseLogging;
/** @brief Prefix prepended to audio module log messages. */
constexpr const char* kAudioPrefix = "[Audio]";
/** @brief Interval, in milliseconds, between periodic audio status logs. */
constexpr unsigned long kAudioLoopDelay = 1UL * 2UL * 1000UL; // 2 seconds

/** @brief Enables recorder module log messages. */
constexpr bool kEnableRecorderLogging = true && kEnableVerboseLogging;
/** @brief Prefix prepended to recorder module log messages. */
constexpr const char* kRecorderPrefix = "[Rec]";

/** @brief Enables trigger module log messages. */
constexpr bool kEnableTriggerLogging = true && kEnableVerboseLogging;
/** @brief Prefix prepended to trigger module log messages. */
constexpr const char* kTriggerPrefix = "[Trig]";

/** @brief Enables BLE module log messages. */
constexpr bool kEnableBLELogging = true && kEnableVerboseLogging;
/** @brief Prefix prepended to BLE module log messages. */
constexpr const char* kBLEPrefix = "[BLE]";
/** @brief Interval, in milliseconds, between periodic BLE status logs. */
constexpr unsigned long kBLELoopDelay = 1UL * 5UL * 1000UL; // 5 seconds

} // namespace debug_config
