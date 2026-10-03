/**
 * @file imu.cpp
 *
 * @brief Implementation of the MPU6050 IMU module.
 *
 * @details
 * Wraps @c Adafruit_MPU6050. @c startIMUModule() brings the sensor up with
 * the ranges from @c imu_config, then a cooperative @c Thread samples it every
 * @c imu_config::kThreadRefreshIntervalMs and keeps the newest sample for
 * other modules to read via @c getLatestImuReading().
 *
 */

#include <Arduino.h>
#include <Thread.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

#include "imu.h"
#include "configs.h"
#include "logger.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the IMU module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief Timestamp for adding debug logs for the IMU loop */
unsigned long nowLoop = 0;

/** @brief @c Adafruit_MPU6050 driver instance */
Adafruit_MPU6050 mpu;

/** @brief Whether @c startIMUModule() succeeded. */
bool started = false;
/** @brief Whether @c latestReading holds at least one sample. */
bool hasReading = false;
/** @brief The newest sample taken by @c imuTick(). */
ImuReading latestReading = {};

/**
 * @brief One cooperative thread tick, sampling the MPU6050.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void imuTick() {
    sensors_event_t a, g, temp;
    if (!mpu.getEvent(&a, &g, &temp)) {
        debug_logs::imuLogging("Failed to read MPU6050 sample.");
        return;
    }

    latestReading.accel[0] = a.acceleration.x;
    latestReading.accel[1] = a.acceleration.y;
    latestReading.accel[2] = a.acceleration.z;
    latestReading.gyro[0] = g.gyro.x;
    latestReading.gyro[1] = g.gyro.y;
    latestReading.gyro[2] = g.gyro.z;
    latestReading.temperatureC = temp.temperature;
    latestReading.timestampMs = millis();
    hasReading = true;
}

/** @brief Thread for sampling the MPU6050. */
Thread imuThread = Thread([]() {
    imuTick();
});

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the IMU module, declared in imu.h.
 * @{
 */
bool startIMUModule() {
    uint8_t attempts = 0;
    while (!mpu.begin()) {
        if (++attempts >= imu_config::kInitAttempts) {
            debug_logs::imuLogging("MPU6050 not found after %u attempts (check SDA=D4, SCL=D5, 3V3, GND).", attempts);
            return false;
        }
        debug_logs::imuLogging("MPU6050 not found on attempt %u. Retrying...", attempts);
        delay(imu_config::kInitIntervalMs);
    }

    mpu.setAccelerometerRange(imu_config::kAccelRange);
    mpu.setGyroRange(imu_config::kGyroRange);
    mpu.setFilterBandwidth(imu_config::kFilterBandwidth);

    imuThread.setInterval(imu_config::kThreadRefreshIntervalMs);
    started = true;

    debug_logs::imuLogging("Started IMU module (accel range %d, gyro range %d, bandwidth %d).",
        mpu.getAccelerometerRange(), mpu.getGyroRange(), mpu.getFilterBandwidth());
    return true;
}

void updateIMUModule() {
    if (!started) return;

    if (imuThread.shouldRun()) imuThread.run();

    if (hasReading && millis() - nowLoop >= debug_config::kIMULoopDelay) {
        debug_logs::imuLogging("Accel [%.2f, %.2f, %.2f] m/s^2 Gyro [%.2f, %.2f, %.2f] rad/s Temp %.1f C",
            latestReading.accel[0], latestReading.accel[1], latestReading.accel[2],
            latestReading.gyro[0], latestReading.gyro[1], latestReading.gyro[2],
            latestReading.temperatureC);
        nowLoop = millis();
    }
}

bool getLatestImuReading(ImuReading& reading) {
    if (!started || !hasReading) return false;
    reading = latestReading;
    return true;
}
/** @} */ // end of Public
