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
#include <Adafruit_MPU6050.h>

namespace main_config {
/** @brief Delay, in milliseconds, between successive loop() iterations. */
inline constexpr uint8_t kRefreshIntervalMs = 10;

} // namespace main_config

namespace led_config {
/** @brief GPIO pin of the XIAO ESP32-C6's onboard user LED (yellow, GPIO15). */
inline constexpr uint8_t kStatusLedPin = 15;

/** @brief When true, the LED is lit by driving its pin LOW (the XIAO's LED is active-low). */
inline constexpr bool kActiveLow = true;

/** @brief Interval, in milliseconds, between status LED thread ticks. */
inline constexpr uint8_t kThreadRefreshIntervalMs = 20;

} // namespace led_config

namespace imu_config {
/**
 * @brief Accelerometer full-scale range applied on module start.
 *
 * @par Options
 * MPU6050_RANGE_2_G, MPU6050_RANGE_4_G, MPU6050_RANGE_8_G, or MPU6050_RANGE_16_G.
 *
 */
inline constexpr mpu6050_accel_range_t kAccelRange = MPU6050_RANGE_8_G;

/**
 * @brief Gyroscope full-scale range applied on module start.
 *
 * @par Options
 * MPU6050_RANGE_250_DEG, MPU6050_RANGE_500_DEG, MPU6050_RANGE_1000_DEG, or
 * MPU6050_RANGE_2000_DEG.
 *
 */
inline constexpr mpu6050_gyro_range_t kGyroRange = MPU6050_RANGE_500_DEG;

/**
 * @brief Digital low-pass filter bandwidth applied on module start.
 *
 * @par Options
 * MPU6050_BAND_260_HZ, MPU6050_BAND_184_HZ, MPU6050_BAND_94_HZ,
 * MPU6050_BAND_44_HZ, MPU6050_BAND_21_HZ, MPU6050_BAND_10_HZ, or
 * MPU6050_BAND_5_HZ.
 *
 */
inline constexpr mpu6050_bandwidth_t kFilterBandwidth = MPU6050_BAND_5_HZ;

/** @brief Delay, in milliseconds, between MPU6050 init attempts. */
inline constexpr unsigned long kInitIntervalMs = 1UL * 1UL * 1000UL; // 1 second
/** @brief Number of attempts made when the MPU6050 fails to initialize. */
inline constexpr uint8_t kInitAttempts = 3;

/** @brief Interval, in milliseconds, between MPU6050 samples (IMU thread ticks). */
inline constexpr uint8_t kThreadRefreshIntervalMs = 50;

} // namespace imu_config

namespace sound_config {
/** @brief Analog pin the sound sensor's output is wired to. */
inline constexpr uint8_t kSoundPin = A0;

/** @brief Number of ADC reads averaged into one sound level sample. */
inline constexpr uint8_t kSamplesPerReading = 32;

/** @brief Interval, in milliseconds, between sound samples (sound thread ticks). */
inline constexpr uint8_t kThreadRefreshIntervalMs = 50;

} // namespace sound_config

namespace debug_config {
/** @brief When true, the onboard status LED reflects device state. */
inline constexpr bool kEnableStatusLight = true;

/** @brief Master switch for all verbose debug logging. */
inline constexpr bool kEnableVerboseLogging = true;
/**
 * @brief Maximum time, in milliseconds, setup() waits for a serial monitor to connect.
 *
 * @details
 * The XIAO ESP32-C6 prints over native USB, so @c Serial only reports ready
 * once a host has the port open. The timeout keeps the device booting when it
 * runs untethered (e.g. on battery).
 *
 */
inline constexpr unsigned long kSerialWaitTimeoutMs = 1UL * 3UL * 1000UL; // 3 seconds
/** @brief Interval, in milliseconds, between flushes of the log queue. */
inline constexpr unsigned long kLoopLogDelay = 1UL * 1UL * 1000UL; // 1 second

/** @brief Enables status LED log messages. */
inline constexpr bool kEnableLEDLogging = true && kEnableVerboseLogging;
/** @brief Prefix prepended to status LED log messages. */
inline constexpr const char* kLEDPrefix = "[LED]";

/** @brief Enables IMU module log messages. */
inline constexpr bool kEnableIMULogging = true && kEnableVerboseLogging;
/** @brief Prefix prepended to IMU module log messages. */
inline constexpr const char* kIMUPrefix = "[IMU]";
/** @brief Interval, in milliseconds, between periodic IMU reading logs. */
inline constexpr unsigned long kIMULoopDelay = 1UL * 1UL * 500UL; // 0.5 seconds

/** @brief Enables sound module log messages. */
inline constexpr bool kEnableSoundLogging = true && kEnableVerboseLogging;
/** @brief Prefix prepended to sound module log messages. */
inline constexpr const char* kSoundPrefix = "[Sound]";
/** @brief Interval, in milliseconds, between periodic sound reading logs. */
inline constexpr unsigned long kSoundLoopDelay = 1UL * 1UL * 500UL; // 0.5 seconds

} // namespace debug_config
