/**
 * @file main.cpp
 *
 * @brief Application entry point wiring together every firmware module.
 *
 * @details
 * @c setup() brings up serial logging, then the status LED, then the IMU
 * (MPU6050) and sound sensor modules, then BLE, which streams both sensors'
 * readings to connected clients. Any failure along the way is reflected on the
 * status LED, and @c setup() blocks on that failed state before falling
 * through to the idle pattern. @c loop() then ticks the status LED, IMU,
 * sound, and BLE modules once per iteration and periodically flushes queued
 * debug logs via @c debug_logs::flushLogs().
 *
 */

#include <Arduino.h>

#include "configs.h"
#include "logger.h"
#include "led/led_handler.h"
#include "imu/imu.h"
#include "sound/sound.h"
#include "ble/ble.h"

namespace {
/** @brief Timestamp for adding debug logs for the main loop */
unsigned long nowLoop = 0;
}

void setup() {
  // Initialize serial for debug logging if enabled. Native USB only reports
  // ready once a monitor is attached, so don't wait on it forever.
  if (debug_config::kEnableVerboseLogging) {
    Serial.begin(115200);
    unsigned long serialStart = millis();
    while (!Serial && millis() - serialStart < debug_config::kSerialWaitTimeoutMs) delay(10);
  }

  // The status LED runs on its own thread, indicating state without blocking other operations.
  startStatusLED();
  setStatusState(BlinkState::Setup);

  // Start the MPU6050 over I2C.
  if (!startIMUModule()) {
    setStatusState(BlinkState::IMUFail);
  }

  // Start the analog sound sensor.
  if (!startSoundModule()) {
    setStatusState(BlinkState::SoundFail);
  }

  // Start BLE advertising, streaming the sensor readings to connected clients.
  if (!startBLEModule()) {
    setStatusState(BlinkState::BLEFail);
  }

  while (inFailedState()) {
    updateStatusLED();
    if (millis() - nowLoop >= debug_config::kLoopLogDelay) {
      debug_logs::flushLogs();
      nowLoop = millis();
    }
    delay(main_config::kRefreshIntervalMs);
  }

  setStatusState(BlinkState::Idle);
}

void loop() {
  updateStatusLED();
  updateIMUModule();
  updateSoundModule();
  updateBLEModule();

  if (millis() - nowLoop >= debug_config::kLoopLogDelay) {
    debug_logs::flushLogs();
    nowLoop = millis();
  }
  delay(main_config::kRefreshIntervalMs);
}
