/**
 * @headerfile imu.h "src/imu/imu.h"
 *
 */

#pragma once

/** @brief One sample from the MPU6050. */
struct ImuReading {
    /** @brief Acceleration along X, Y, Z, in m/s^2. */
    float accel[3];
    /** @brief Angular rate around X, Y, Z, in rad/s. */
    float gyro[3];
    /** @brief Die temperature, in degrees Celsius. */
    float temperatureC;
    /** @brief millis() timestamp the sample was taken at. */
    unsigned long timestampMs;
};

/**
 * @brief Start the IMU module and configure the MPU6050.
 *
 * @details
 * Attempts to initialize the MPU6050 over I2C (SDA=D4, SCL=D5) up to @c
 * imu_config::kInitAttempts times, @c imu_config::kInitIntervalMs apart, then
 * applies the accelerometer range, gyro range, and filter bandwidth from @c
 * imu_config and starts the module's sampling thread.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the IMU module startup attempt.
 * @retval true The MPU6050 was found and configured.
 * @retval false The MPU6050 did not respond after every attempt.
 *
 */
bool startIMUModule();

/**
 * @brief Run the IMU sampling thread tick, if it is due.
 *
 * @details
 * Call regularly from the main loop. Once every @c
 * imu_config::kThreadRefreshIntervalMs, reads a new sample into the module's
 * latest reading, see @c getLatestImuReading(). Periodically logs that reading
 * every @c debug_config::kIMULoopDelay.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateIMUModule();

/**
 * @brief Copy out the most recent MPU6050 sample.
 *
 * @param reading Destination that receives the latest sample.
 *
 * @return Whether a sample was available.
 * @retval true @p reading was filled with the latest sample.
 * @retval false The module has not started, or no sample has been taken yet.
 *
 */
bool getLatestImuReading(ImuReading& reading);
