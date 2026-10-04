/**
 * @file audio.cpp
 *
 * @brief Implementation of the continuous microphone capture module.
 *
 * @details
 * The IM69D130 microphone streams PDM audio into the NDP120, which buffers it
 * as 16 kHz, 16-bit mono PCM chunks of ~16 ms. A cooperative @c Thread
 * extracts every chunk as it appears and passes it on (to the recorder), so
 * the audio stream is continuous.
 *
 * Each chunk the NDP returns is followed by a 4-byte annotation whose third
 * byte is a per-chunk counter. A repeated counter means no new chunk yet (it's
 * skipped), and a jump means chunks were missed (reported as a gap, e.g. after
 * a slow flash erase). If the counter turns out never to change, it's
 * distrusted and every extraction counts as a new chunk, so audio is never
 * silently dropped.
 *
 * The chunk buffer is allocated once, at exactly the size the NDP reports.
 *
 */

#include <Arduino.h>
#include <math.h>
#include <new>
#include <NDP.h>

#include "audio.h"
#include "../ndp/ndp_module.h"
#include "../configs.h"
#include "../logger.h"
#include "../module_utils.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the audio module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief Offset of the per-chunk counter within the NDP's 4-byte annotation. */
constexpr uint8_t kCounterOffset = 2;
/** @brief How long, in milliseconds, the counter may stay unchanged before it's distrusted. */
constexpr unsigned long kStuckCounterMs = 500;

/** @brief Buffer one extracted chunk (samples + annotation) lands in, sized in @c startAudioModule(). Never freed. */
uint8_t* chunkBuffer = nullptr;
/** @brief Receives every new chunk, see @c setAudioChunkHandler(). */
AudioChunkHandler chunkHandler = nullptr;

/** @brief Whether @c lastCounter holds a real counter value yet. */
bool haveCounter = false;
/** @brief Counter of the last chunk passed on. */
uint8_t lastCounter = 0;
/** @brief Whether repeated counters mean "no new chunk" (see the file comment). */
bool counterTrusted = true;
/** @brief millis() when a new counter value was last seen. */
unsigned long lastNewChunkMs = 0;

/** @brief RMS loudness of the newest chunk. */
uint16_t soundLevel = 0;
/** @brief Chunks passed on since the last status log. */
uint16_t chunksSinceLog = 0;
/** @brief Chunks reported missed since the last status log. */
uint16_t missedSinceLog = 0;

/**
 * @brief RMS amplitude of a block of 16-bit samples.
 *
 * @param samples The samples.
 * @param count Number of samples, at least 1.
 *
 * @return RMS amplitude, 0 to 32767.
 *
 */
uint16_t rmsLevel(const int16_t* samples, size_t count) {
    uint64_t sumSquares = 0;
    for (size_t i = 0; i < count; ++i) {
        int32_t s = samples[i];
        sumSquares += static_cast<uint64_t>(s * s);
    }
    float rms = sqrtf(static_cast<float>(sumSquares) / count);
    return rms > 32767.0f ? 32767 : static_cast<uint16_t>(rms + 0.5f);
}

/**
 * @brief Extract one chunk and pass it on if it's new.
 *
 * @return Whether a new chunk was extracted (false: nothing new, or an error).
 *
 */
bool extractOneChunk() {
    unsigned int len = 0;
    if (NDP.extractData(chunkBuffer, &len) || len < sizeof(int16_t)) return false;

    uint8_t counter = chunkBuffer[len + kCounterOffset];
    uint8_t missed = 0;
    unsigned long now = millis();
    if (counterTrusted && haveCounter) {
        if (counter == lastCounter) {
            if (now - lastNewChunkMs > kStuckCounterMs) {
                counterTrusted = false;
                debug_logs::audioLogging("NDP chunk counter never changes, extracting without it.");
            } else {
                return false; // no new chunk yet
            }
        } else {
            missed = static_cast<uint8_t>(counter - lastCounter - 1);
        }
    }
    haveCounter = true;
    lastCounter = counter;
    lastNewChunkMs = now;

    const int16_t* samples = reinterpret_cast<const int16_t*>(chunkBuffer);
    size_t count = len / sizeof(int16_t);
    soundLevel = rmsLevel(samples, count);
    chunksSinceLog++;
    missedSinceLog += missed;

    if (chunkHandler != nullptr) chunkHandler(samples, count, missed, now);
    return true;
}

/**
 * @brief One extraction tick: drain up to @c audio_config::kMaxChunksPerTick new chunks.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void audioTick() {
    for (uint8_t i = 0; i < audio_config::kMaxChunksPerTick; ++i) {
        if (!extractOneChunk()) break;
        if (!counterTrusted) break; // without the counter, one chunk per tick
    }
}

/**
 * @brief One logging tick, queuing the chunk rate, gaps, and loudness.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void audioLogTick() {
    debug_logs::audioLogging("%u chunks, %u missed, level %u", chunksSinceLog, missedSinceLog, soundLevel);
    chunksSinceLog = 0;
    missedSinceLog = 0;
}

/** @brief Thread for extracting audio. */
Thread audioThread = makeIdleThread(audioTick, audio_config::kExtractIntervalMs);
/** @brief Thread for periodically logging capture status. */
Thread audioLogThread = makeIdleThread(audioLogTick, debug_config::kAudioLoopDelay);

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the audio module, declared in audio.h.
 * @{
 */
bool startAudioModule() {
    if (!isNDPReady()) {
        debug_logs::audioLogging("NDP is not ready, the microphone is unreachable.");
        return false;
    }

    if (NDP.turnOnMicrophone()) {
        debug_logs::audioLogging("Failed to turn on the microphone.");
        return false;
    }

    // Includes the 4-byte annotation that follows the samples.
    int chunkBytes = NDP.getAudioChunkSize();
    if (chunkBytes <= 0) {
        debug_logs::audioLogging("The NDP reported no audio chunk size.");
        return false;
    }

    // Rounded up to whole 32-bit words: the NDP's SPI reads write in 4-byte units.
    chunkBuffer = new (std::nothrow) uint8_t[(chunkBytes + 3) & ~3];
    if (chunkBuffer == nullptr) {
        debug_logs::audioLogging("Not enough memory for a %d byte audio chunk.", chunkBytes);
        return false;
    }

    audioThread.enabled = true;
    audioLogThread.enabled = true;

    debug_logs::audioLogging("Started audio capture (%d byte chunks).", chunkBytes);
    return true;
}

void updateAudioModule() {
    runIfDue(audioThread);
    runIfDue(audioLogThread);
}

void setAudioChunkHandler(AudioChunkHandler handler) {
    chunkHandler = handler;
}

uint16_t getSoundLevel() {
    return soundLevel;
}
/** @} */ // end of Public
