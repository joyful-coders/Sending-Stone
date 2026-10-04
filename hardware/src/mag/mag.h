/**
 * @headerfile mag.h "src/mag/mag.h"
 *
 */

#pragma once

/** @brief One compensated sample from the BMM150. */
struct MagReading {
    /** @brief Magnetic field along X, Y, Z, in microtesla. */
    float field[3];
    /** @brief millis() timestamp the sample was taken at. */
    unsigned long timestampMs;
};

/**
 * @brief Start the magnetometer module and configure the onboard BMM150.
 *
 * @details
 * The BMM150 sits on the NDP120's SPI bus, so this requires @c
 * startNDPModule() to have succeeded. Powers the BMM150 up, checks its chip
 * ID, reads its factory trim registers (needed to convert raw counts to
 * microtesla), and puts it in normal mode at 20 Hz with Bosch's "regular"
 * repetition preset, retrying up to @c mag_config::kInitAttempts times.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the magnetometer module startup attempt.
 * @retval true The BMM150 was found and configured.
 * @retval false The NDP isn't ready, or the BMM150 never answered with its chip ID.
 *
 */
bool startMagModule();

/**
 * @brief Run the magnetometer sampling thread tick, if it is due.
 *
 * @details
 * Call regularly from the main loop. Once every @c
 * mag_config::kThreadRefreshIntervalMs, reads and compensates a new sample
 * into the module's latest reading, see @c getLatestMagReading(). Periodically
 * logs that reading every @c debug_config::kMagLoopDelay.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateMagModule();

/**
 * @brief Copy out the most recent BMM150 sample.
 *
 * @param reading Destination that receives the latest sample.
 *
 * @return Whether a sample was available.
 * @retval true @p reading was filled with the latest sample.
 * @retval false The module has not started, or no valid sample has been taken yet.
 *
 */
bool getLatestMagReading(MagReading& reading);
