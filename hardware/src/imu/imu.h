/**
 * @headerfile imu.h "src/imu/imu.h"
 *
 */

#pragma once

/** @brief One sample from the BMI270. */
struct ImuReading {
    /** @brief Acceleration along X, Y, Z, in m/s^2. */
    float accel[3];
    /** @brief Angular rate around X, Y, Z, in rad/s. */
    float gyro[3];
    /** @brief BMI270 die temperature, in degrees Celsius. */
    float temperatureC;
    /** @brief millis() timestamp the sample was taken at. */
    unsigned long timestampMs;
};

/**
 * @brief Start the IMU module and configure the onboard BMI270.
 *
 * @details
 * The BMI270 sits on the NDP120's SPI bus, so this requires @c
 * startNDPModule() to have succeeded. Soft-resets the BMI270, uploads its
 * configuration blob (see bmi270_config.h), retrying up to @c
 * imu_config::kInitAttempts times, then enables the accelerometer, gyroscope,
 * and temperature sensor with the ranges from @c imu_config and starts the
 * module's sampling thread.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the IMU module startup attempt.
 * @retval true The BMI270 was found and configured.
 * @retval false The NDP isn't ready, the chip ID didn't match, or the config upload never succeeded.
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
 * @brief Copy out the most recent BMI270 sample.
 *
 * @param reading Destination that receives the latest sample.
 *
 * @return Whether a sample was available.
 * @retval true @p reading was filled with the latest sample.
 * @retval false The module has not started, or no sample has been taken yet.
 *
 */
bool getLatestImuReading(ImuReading& reading);
