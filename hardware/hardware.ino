/**
 * @file hardware.ino
 *
 * @brief Application entry point wiring together every firmware module.
 *
 * @details
 * Firmware for the Arduino Nicla Voice (ABX00061). @c setup() brings up
 * serial logging and the Nicla's power management, then the status LED, then
 * BLE (first, for heap reasons, see below), which streams every sensor's
 * readings to connected clients, then the NDP120 (which every onboard sensor
 * hangs off), then the BMI270 IMU, BMM150 magnetometer, microphone, and
 * battery monitor. Any failure along the
 * way is reflected on the status LED, and @c setup() blocks on that failed
 * state before falling through to the idle pattern. @c loop() then ticks every
 * module once per iteration and periodically flushes queued debug logs via
 * @c debug_logs::flushLogs().
 *
 */

#include <Arduino.h>
#include <Nicla_System.h>

#include "src/configs.h"
#include "src/logger.h"
#include "src/led/led_handler.h"
#include "src/ndp/ndp_module.h"
#include "src/imu/imu.h"
#include "src/mag/mag.h"
#include "src/sound/sound.h"
#include "src/battery/battery.h"
#include "src/ble/ble.h"
#include "src/module_utils.h"

namespace {
/** @brief Thread printing the queued debug logs every @c debug_config::kLoopLogDelay. */
Thread logFlushThread = Thread([]() { debug_logs::flushLogs(); }, debug_config::kLoopLogDelay);

/**
 * @brief Flush queued logs if the flush interval has elapsed.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void flushLogsIfDue() {
  runIfDue(logFlushThread);
}
}

void setup() {
  // Initialize serial for debug logging if enabled, without waiting forever
  // when no monitor is attached (e.g. running on battery).
  if (debug_config::kEnableVerboseLogging) {
    Serial.begin(115200);
    unsigned long serialStart = millis();
    while (!Serial && millis() - serialStart < debug_config::kSerialWaitTimeoutMs) delay(10);
  }

  // Power management and the I2C bus the LED driver and PMIC share. The header
  // pins' LDO is unused (no external hardware), so it's turned off to save power.
  nicla::begin();
  nicla::disableLDO();

  // The status LED runs on its own thread, indicating state without blocking other operations.
  startStatusLED();
  setStatusState(BlinkState::Setup);
  updateStatusLED();

  // BLE goes first: its controller needs one contiguous 13 KB heap block, which
  // is only guaranteed before the NDP firmware load churns the heap with file
  // buffers. Packets only flow once the IMU has a reading, so advertising early
  // is harmless.
  if (!startBLEModule()) {
    setStatusState(BlinkState::BLEFail);
  // Load the NDP120's firmware. The IMU, magnetometer, and microphone all sit behind it.
  } else if (!startNDPModule()) {
    setStatusState(BlinkState::NDPFail);
  } else if (!startIMUModule()) {
    setStatusState(BlinkState::IMUFail);
  } else if (!startMagModule()) {
    setStatusState(BlinkState::MagFail);
  } else if (!startSoundModule()) {
    setStatusState(BlinkState::SoundFail);
  }

  startBatteryModule();

  while (inFailedState()) {
    updateStatusLED();
    flushLogsIfDue();
    delay(main_config::kRefreshIntervalMs);
  }

  setStatusState(BlinkState::Idle);
}

void loop() {
  updateStatusLED();
  updateIMUModule();
  updateMagModule();
  updateSoundModule();
  updateBatteryModule();
  updateBLEModule();

  flushLogsIfDue();
  delay(main_config::kRefreshIntervalMs);
}
