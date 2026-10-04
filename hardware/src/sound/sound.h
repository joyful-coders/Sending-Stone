/**
 * @headerfile sound.h "src/sound/sound.h"
 *
 */

#pragma once

#include <cstdint>

/** @brief One loudness sample from the onboard microphone. */
struct SoundReading {
    /** @brief RMS amplitude of one audio chunk of 16-bit PCM, 0 (silence) to 32767 (full scale). */
    uint16_t level;
    /** @brief millis() timestamp the sample was taken at. */
    unsigned long timestampMs;
};

/**
 * @brief Start the sound module and the onboard IM69D130 microphone.
 *
 * @details
 * The microphone is a PDM device feeding the NDP120, so this requires @c
 * startNDPModule() to have succeeded. Turns the microphone on, allocates a
 * buffer of exactly one audio chunk (the size the NDP reports), and starts the
 * module's sampling thread.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the sound module startup attempt.
 * @retval true The microphone is on and audio chunks can be extracted.
 * @retval false The NDP isn't ready, the microphone failed to start, or there's no memory for the buffer.
 *
 */
bool startSoundModule();

/**
 * @brief Run the sound sampling thread tick, if it is due.
 *
 * @details
 * Call regularly from the main loop. Once every @c
 * sound_config::kThreadRefreshIntervalMs, extracts the newest audio chunk
 * (16 kHz, 16-bit mono PCM) from the NDP and stores its RMS amplitude as the
 * module's latest reading, see @c getLatestSoundReading(). Periodically logs
 * that reading every @c debug_config::kSoundLoopDelay.
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
