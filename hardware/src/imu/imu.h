/**
 * @headerfile imu.h "src/imu/imu.h"
 *
 */

#pragma once

#include <cstdint>

/** @brief One raw sample from the BMI270, in sensor counts. */
struct ImuSample {
    /** @brief Acceleration along X, Y, Z, in counts. See @c imuAccelGPerCount(). */
    int16_t accel[3];
    /** @brief Angular rate around X, Y, Z, in counts. See @c imuGyroDpsPerCount(). */
    int16_t gyro[3];
    /** @brief millis() timestamp the sample was taken at. */
    uint32_t timestampMs;
};

/** @brief Function called with every new IMU sample, see @c setImuSampleHandler(). */
typedef void (*ImuSampleHandler)(const ImuSample& sample);

/**
 * @brief Start the IMU module and configure the onboard BMI270.
 *
 * @details
 * The BMI270 sits on the NDP120's SPI bus, so this requires @c
 * startNDPModule() to have succeeded. Soft-resets the BMI270, uploads its
 * configuration blob (see bmi270_config.h), retrying up to @c
 * imu_config::kInitAttempts times, then enables the accelerometer and
 * gyroscope with the ranges from @c imu_config and starts the module's
 * sampling thread.
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
 * imu_config::kThreadRefreshIntervalMs, reads a new sample and passes it to
 * every registered handler.
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
 * @brief Register a function to receive every new sample.
 *
 * @param handler Function to call, from the main loop, with each sample.
 *
 * @return Whether the handler was registered (up to 4 are allowed).
 *
 */
bool addImuSampleHandler(ImuSampleHandler handler);

/** @brief g per accelerometer count at the configured range. */
float imuAccelGPerCount();

/** @brief Degrees per second per gyroscope count at the configured range. */
float imuGyroDpsPerCount();
