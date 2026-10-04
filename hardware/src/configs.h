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

namespace main_config {
/** @brief Delay, in milliseconds, between successive loop() iterations. */
constexpr uint8_t kRefreshIntervalMs = 5;

} // namespace main_config

namespace led_config {
/**
 * @brief Brightness of the onboard RGB LED.
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
 * required for the NDP's audio pipeline (and so the microphone) to run. These
 * ship preloaded on the Nicla Voice, see README.md if they need re-uploading.
 *
 */
constexpr char kMcuFirmware[] = "mcu_fw_120_v91.synpkg";
/** @brief DSP firmware package, see @c kMcuFirmware. */
constexpr char kDspFirmware[] = "dsp_firmware_v91.synpkg";
/** @brief Neural network model package, see @c kMcuFirmware. */
constexpr char kModel[] = "alexa_334_NDP120_B0_v11_v91.synpkg";

} // namespace ndp_config

namespace imu_config {
/**
 * @brief BMI270 accelerometer full-scale range, in g.
 *
 * @par Options
 * 2, 4, 8, or 16.
 *
 */
constexpr uint8_t kAccelRangeG = 8;

/**
 * @brief BMI270 gyroscope full-scale range, in degrees per second.
 *
 * @note
 * The BLE packet's gyro fields saturate at +-32.767 rad/s (~1877 dps), so
 * 2000 clips at the very top of its range.
 *
 * @par Options
 * 125, 250, 500, 1000, or 2000.
 *
 */
constexpr uint16_t kGyroRangeDps = 1000;

/** @brief Number of attempts made when the BMI270 fails to initialize. */
constexpr uint8_t kInitAttempts = 3;

/** @brief Interval, in milliseconds, between BMI270 samples (IMU thread ticks). */
constexpr uint8_t kThreadRefreshIntervalMs = 50;

} // namespace imu_config

namespace mag_config {
/** @brief Number of attempts made when the BMM150 fails to initialize. */
constexpr uint8_t kInitAttempts = 3;

/** @brief Interval, in milliseconds, between BMM150 samples (mag thread ticks). Its ODR is 20 Hz. */
constexpr uint8_t kThreadRefreshIntervalMs = 50;

} // namespace mag_config

namespace sound_config {
/** @brief Interval, in milliseconds, between microphone level samples (sound thread ticks). */
constexpr uint8_t kThreadRefreshIntervalMs = 50;

} // namespace sound_config

namespace battery_config {
/** @brief Interval, in milliseconds, between PMIC battery reads (battery thread ticks). */
constexpr unsigned long kThreadRefreshIntervalMs = 1UL * 5UL * 1000UL; // 5 seconds

} // namespace battery_config

namespace ble_config {
/** @brief Device name shown to scanning clients. */
constexpr char kDeviceName[] = "Nicla-Sensors";

/** @brief UUID of the sensor data service, advertised so clients can filter scans by it. */
constexpr char kServiceUuid[] = "db118277-ac3c-4312-9c3f-8f0f77e70acc";
/** @brief UUID of the readings characteristic (READ + NOTIFY), carrying one @c SensorPacket. */
constexpr char kReadingsCharUuid[] = "4f2e8c83-317d-4a69-99bc-c9006d46e64e";
/** @brief UUID of the packet version characteristic (READ), carrying @c BLE_PACKET_VERSION. */
constexpr char kVersionCharUuid[] = "48e4c40b-6e01-49f1-b99b-0e6e9bd77a16";

/**
 * @brief Interval, in milliseconds, between sensor packet notifications (BLE thread ticks).
 *
 * @details
 * Defaults to the IMU's sample interval so every IMU sample is sent once.
 * Shorter than that resends the same sample, longer than that skips samples.
 *
 * @par Options
 * Any positive duration. Below ~15ms exceeds what most BLE connection
 * intervals can deliver and notifications will queue or drop.
 *
 */
constexpr uint16_t kNotifyIntervalMs = imu_config::kThreadRefreshIntervalMs;

} // namespace ble_config

namespace debug_config {
/** @brief When true, the onboard status LED reflects device state. */
constexpr bool kEnableStatusLight = true;

/** @brief Master switch for all verbose debug logging. */
constexpr bool kEnableVerboseLogging = true;
/** @brief Maximum time, in milliseconds, setup() waits for a serial monitor to connect. */
constexpr unsigned long kSerialWaitTimeoutMs = 1UL * 3UL * 1000UL; // 3 seconds
/** @brief Interval, in milliseconds, between flushes of the log queue. */
constexpr unsigned long kLoopLogDelay = 1UL * 1UL * 1000UL; // 1 second

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
constexpr unsigned long kIMULoopDelay = 1UL * 1UL * 500UL; // 0.5 seconds

/** @brief Enables magnetometer module log messages. */
constexpr bool kEnableMagLogging = true && kEnableVerboseLogging;
/** @brief Prefix prepended to magnetometer module log messages. */
constexpr const char* kMagPrefix = "[Mag]";
/** @brief Interval, in milliseconds, between periodic magnetometer reading logs. */
constexpr unsigned long kMagLoopDelay = 1UL * 1UL * 500UL; // 0.5 seconds

/** @brief Enables sound module log messages. */
constexpr bool kEnableSoundLogging = true && kEnableVerboseLogging;
/** @brief Prefix prepended to sound module log messages. */
constexpr const char* kSoundPrefix = "[Sound]";
/** @brief Interval, in milliseconds, between periodic sound reading logs. */
constexpr unsigned long kSoundLoopDelay = 1UL * 1UL * 500UL; // 0.5 seconds

/** @brief Enables battery module log messages. */
constexpr bool kEnableBatteryLogging = true && kEnableVerboseLogging;
/** @brief Prefix prepended to battery module log messages. */
constexpr const char* kBatteryPrefix = "[Battery]";

/** @brief Enables BLE module log messages. */
constexpr bool kEnableBLELogging = true && kEnableVerboseLogging;
/** @brief Prefix prepended to BLE module log messages. */
constexpr const char* kBLEPrefix = "[BLE]";
/** @brief Interval, in milliseconds, between periodic BLE status logs. */
constexpr unsigned long kBLELoopDelay = 1UL * 5UL * 1000UL; // 5 seconds

} // namespace debug_config
