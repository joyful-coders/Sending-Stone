/**
 * @file sound.cpp
 *
 * @brief Implementation of the analog sound sensor module.
 *
 * @details
 * A cooperative @c Thread averages a burst of ADC reads every @c
 * sound_config::kThreadRefreshIntervalMs to smooth out the sensor's noise, and
 * keeps the newest averaged level for other modules to read via @c
 * getLatestSoundReading().
 *
 */

#include <Arduino.h>
#include <Thread.h>

#include "sound.h"
#include "configs.h"
#include "logger.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the sound module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief Timestamp for adding debug logs for the sound loop */
unsigned long nowLoop = 0;

/** @brief Whether @c startSoundModule() succeeded. */
bool started = false;
/** @brief Whether @c latestReading holds at least one sample. */
bool hasReading = false;
/** @brief The newest sample taken by @c soundTick(). */
SoundReading latestReading = {};

/**
 * @brief One cooperative thread tick, averaging a burst of ADC reads.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void soundTick() {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < sound_config::kSamplesPerReading; ++i) {
        sum += analogRead(sound_config::kSoundPin);
    }

    latestReading.level = sum / sound_config::kSamplesPerReading;
    latestReading.timestampMs = millis();
    hasReading = true;
}

/** @brief Thread for sampling the sound sensor. */
Thread soundThread = Thread([]() {
    soundTick();
});

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the sound module, declared in sound.h.
 * @{
 */
bool startSoundModule() {
    if (digitalPinToAnalogChannel(sound_config::kSoundPin) < 0) {
        debug_logs::soundLogging("Pin %u is not ADC capable.", sound_config::kSoundPin);
        return false;
    }

    pinMode(sound_config::kSoundPin, INPUT);

    soundThread.setInterval(sound_config::kThreadRefreshIntervalMs);
    started = true;

    debug_logs::soundLogging("Started sound module on pin %u.", sound_config::kSoundPin);
    return true;
}

void updateSoundModule() {
    if (!started) return;

    if (soundThread.shouldRun()) soundThread.run();

    if (hasReading && millis() - nowLoop >= debug_config::kSoundLoopDelay) {
        debug_logs::soundLogging("Level %u", latestReading.level);
        nowLoop = millis();
    }
}

bool getLatestSoundReading(SoundReading& reading) {
    if (!started || !hasReading) return false;
    reading = latestReading;
    return true;
}
/** @} */ // end of Public
