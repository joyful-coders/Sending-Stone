/**
 * @file sound.cpp
 *
 * @brief Implementation of the onboard microphone loudness module.
 *
 * @details
 * The IM69D130 microphone streams PDM audio into the NDP120, which buffers it
 * as 16 kHz, 16-bit mono PCM. A cooperative @c Thread extracts the newest
 * chunk every @c sound_config::kThreadRefreshIntervalMs and reduces it to one
 * RMS amplitude, kept for other modules to read via @c getLatestSoundReading().
 * Audio between ticks is skipped, which is fine for a loudness level.
 *
 * The chunk buffer is allocated once, at exactly the size the NDP reports,
 * rather than reserved statically at a worst-case size.
 *
 */

#include <Arduino.h>
#include <math.h>
#include <new>
#include <NDP.h>

#include "sound.h"
#include "../ndp/ndp_module.h"
#include "../configs.h"
#include "../logger.h"
#include "../module_utils.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the sound module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief The newest sample taken by @c soundTick(). */
LatestReading<SoundReading> latest;

/** @brief Buffer one extracted audio chunk lands in, sized in @c startSoundModule(). Never freed. */
uint8_t* audioBuffer = nullptr;

/**
 * @brief One cooperative thread tick, reducing the newest audio chunk to an RMS level.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void soundTick() {
    unsigned int len = 0;
    if (NDP.extractData(audioBuffer, &len) || len < sizeof(int16_t)) return; // no new chunk yet

    const int16_t* samples = reinterpret_cast<const int16_t*>(audioBuffer);
    size_t count = len / sizeof(int16_t);

    uint64_t sumSquares = 0;
    for (size_t i = 0; i < count; ++i) {
        int32_t s = samples[i];
        sumSquares += static_cast<uint64_t>(s * s);
    }

    float rms = sqrtf(static_cast<float>(sumSquares) / count);
    latest.store(SoundReading{rms > 32767.0f ? uint16_t(32767) : static_cast<uint16_t>(rms + 0.5f), millis()});
}

/**
 * @brief One logging tick, queuing the newest sample.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void soundLogTick() {
    if (latest.valid()) debug_logs::soundLogging("Level %u", latest.value().level);
}

/** @brief Thread for sampling the microphone. */
Thread soundThread = makeIdleThread(soundTick, sound_config::kThreadRefreshIntervalMs);
/** @brief Thread for periodically logging the newest sample. */
Thread soundLogThread = makeIdleThread(soundLogTick, debug_config::kSoundLoopDelay);

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the sound module, declared in sound.h.
 * @{
 */
bool startSoundModule() {
    if (!isNDPReady()) {
        debug_logs::soundLogging("NDP is not ready, the microphone is unreachable.");
        return false;
    }

    if (NDP.turnOnMicrophone()) {
        debug_logs::soundLogging("Failed to turn on the microphone.");
        return false;
    }

    int chunkBytes = NDP.getAudioChunkSize();
    if (chunkBytes <= 0) {
        debug_logs::soundLogging("The NDP reported no audio chunk size.");
        return false;
    }

    // Rounded up to whole 32-bit words: the NDP's SPI reads write in 4-byte units.
    audioBuffer = new (std::nothrow) uint8_t[(chunkBytes + 3) & ~3];
    if (audioBuffer == nullptr) {
        debug_logs::soundLogging("Not enough memory for a %d byte audio chunk.", chunkBytes);
        return false;
    }

    soundThread.enabled = true;
    soundLogThread.enabled = true;

    debug_logs::soundLogging("Started sound module (%d byte audio chunks).", chunkBytes);
    return true;
}

void updateSoundModule() {
    runIfDue(soundThread);
    runIfDue(soundLogThread);
}

bool getLatestSoundReading(SoundReading& reading) {
    return latest.copyTo(reading);
}
/** @} */ // end of Public
