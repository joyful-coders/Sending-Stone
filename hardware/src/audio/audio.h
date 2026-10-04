/**
 * @headerfile audio.h "src/audio/audio.h"
 *
 */

#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @brief Function called with every new audio chunk, see @c setAudioChunkHandler().
 *
 * @param samples 16 kHz, 16-bit mono PCM samples.
 * @param count Number of samples.
 * @param missedChunks Chunks the NDP produced since the previous call that were never extracted (a gap).
 * @param timestampMs millis() when the chunk was extracted.
 *
 */
typedef void (*AudioChunkHandler)(const int16_t* samples, size_t count, uint8_t missedChunks, uint32_t timestampMs);

/**
 * @brief Start the audio module and the onboard IM69D130 microphone.
 *
 * @details
 * The microphone is a PDM device feeding the NDP120, so this requires @c
 * startNDPModule() to have succeeded. Turns the microphone on, allocates a
 * buffer of exactly one audio chunk (the size the NDP reports), and starts
 * continuous extraction.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the audio module startup attempt.
 * @retval true The microphone is on and audio chunks can be extracted.
 * @retval false The NDP isn't ready, the microphone failed to start, or there's no memory for the buffer.
 *
 */
bool startAudioModule();

/**
 * @brief Extract any new audio from the NDP, if the extraction thread is due.
 *
 * @details
 * Call every main loop iteration. Every @c audio_config::kExtractIntervalMs,
 * extracts up to @c audio_config::kMaxChunksPerTick new chunks (16 kHz,
 * 16-bit mono PCM), updates the loudness level, and passes each chunk to the
 * handler. Repeated chunks are skipped using the NDP's per-chunk counter.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateAudioModule();

/**
 * @brief Register the function that receives every new audio chunk.
 *
 * @param handler Function to call, from the main loop, with each chunk.
 *
 * @par Returns
 * Nothing.
 *
 */
void setAudioChunkHandler(AudioChunkHandler handler);

/**
 * @brief The RMS loudness of the newest audio chunk.
 *
 * @return RMS amplitude of 16-bit PCM, 0 (silence) to 32767 (full scale), or 0 before the first chunk.
 *
 */
uint16_t getSoundLevel();
