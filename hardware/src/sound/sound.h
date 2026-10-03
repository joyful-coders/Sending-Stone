/**
 * @headerfile sound.h "src/sound/sound.h"
 *
 */

#pragma once

#include <cstdint>

/** @brief One averaged sample from the sound sensor. */
struct SoundReading {
    /** @brief Raw ADC level averaged over @c sound_config::kSamplesPerReading reads. */
    uint16_t level;
    /** @brief millis() timestamp the sample was taken at. */
    unsigned long timestampMs;
};

/**
 * @brief Start the sound module and configure its analog pin.
 *
 * @details
 * Verifies @c sound_config::kSoundPin is ADC capable, sets it as an input, and
 * starts the module's sampling thread.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the sound module startup attempt.
 * @retval true The pin was configured and sampling started.
 * @retval false @c sound_config::kSoundPin is not an ADC capable pin.
 *
 */
bool startSoundModule();

/**
 * @brief Run the sound sampling thread tick, if it is due.
 *
 * @details
 * Call regularly from the main loop. Once every @c
 * sound_config::kThreadRefreshIntervalMs, averages @c
 * sound_config::kSamplesPerReading ADC reads into the module's latest reading,
 * see @c getLatestSoundReading(). Periodically logs that reading every @c
 * debug_config::kSoundLoopDelay.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateSoundModule();

/**
 * @brief Copy out the most recent sound sample.
 *
 * @param reading Destination that receives the latest sample.
 *
 * @return Whether a sample was available.
 * @retval true @p reading was filled with the latest sample.
 * @retval false The module has not started, or no sample has been taken yet.
 *
 */
bool getLatestSoundReading(SoundReading& reading);
