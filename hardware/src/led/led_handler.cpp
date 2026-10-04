/**
 * @file led_handler.cpp
 *
 * @brief Implementation of the status LED handler.
 *
 * @details
 * @c BLINK_STATES (led_handler.h) expands into a constant table of patterns
 * kept in flash, indexed by @c BlinkState. Only the current state plays, so
 * the only RAM is one playback position. A cooperative @c Thread advances it
 * every @c led_config::kThreadRefreshIntervalMs, writing the Nicla's RGB LED
 * (over I2C via @c nicla::leds) only when a step changes.
 *
 */

#include <Arduino.h>
#include <Nicla_System.h>

#include "led_handler.h"
#include "../configs.h"
#include "../logger.h"
#include "../module_utils.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the led module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/**
 * @defgroup LEDPatterns
 * Blink timing for each @c BLINK_STATES entry: step durations in
 * milliseconds, alternating on, off, on, off... starting with "on".
 * @{
 */
constexpr uint16_t kSetupPattern[]     = {250, 250};
constexpr uint16_t kNDPFailPattern[]   = {500, 500};
constexpr uint16_t kIMUFailPattern[]   = {100, 100, 100, 700};
constexpr uint16_t kMagFailPattern[]   = {100, 100, 100, 100, 100, 700};
constexpr uint16_t kSoundFailPattern[] = {500, 500};
constexpr uint16_t kBLEFailPattern[]   = {600, 200, 100, 700};
constexpr uint16_t kIdlePattern[]      = {50, 1950};
/** @} */ // end of LEDPatterns

/** @brief One @c BLINK_STATES entry, resolved to its pattern. */
struct LedPattern {
    /** @brief Step durations, even indices on, odd indices off. */
    const uint16_t* steps;
    /** @brief Number of entries in @c steps. */
    uint8_t length;
    /** @brief Color shown during "on" steps. */
    RGBColors color;
    /** @brief Whether this state counts as failed, see @c inFailedState(). */
    bool failed;
    /** @brief The state's name, for log messages. */
    const char* name;
    /** @brief Message logged once when the state becomes active. */
    const char* message;
};

/** @brief Every state's pattern, in @c BlinkState order. */
constexpr LedPattern kPatterns[] = {
#define X(name, color, failed, message) \
    {k##name##Pattern, sizeof(k##name##Pattern) / sizeof(k##name##Pattern[0]), color, failed, #name, message},
    BLINK_STATES(X)
#undef X
};

/** @brief The current state of the status LED */
BlinkState currentState = BlinkState::Idle;
/** @brief Index of the step currently playing in the current pattern. */
uint8_t stepIndex = 0;
/** @brief millis() timestamp the current step started at. */
unsigned long stepStart = 0;

/**
 * @brief The pattern for the current state.
 *
 * @return The @c kPatterns entry for @c currentState.
 *
 */
const LedPattern& currentPattern() {
    return kPatterns[static_cast<uint8_t>(currentState)];
}

/**
 * @brief Drive the RGB LED for the current step.
 *
 * @par Returns
 * Nothing.
 *
 */
void showCurrentStep() {
    bool on = (stepIndex % 2) == 0;
    nicla::leds.setColor(on ? currentPattern().color : off);
}

/**
 * @brief Thread callback that advances the current pattern when its step has elapsed.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void statusLedTick() {
    const LedPattern& pattern = currentPattern();
    unsigned long now = millis();
    if (now - stepStart < pattern.steps[stepIndex]) return;

    stepStart = now;
    stepIndex = (stepIndex + 1) % pattern.length;
    showCurrentStep();
}

/** @brief The thread for managing the status LED updates. */
Thread statusLedThread = makeIdleThread(statusLedTick, led_config::kThreadRefreshIntervalMs);

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the led module, declared in led_handler.h.
 * @{
 */
bool startStatusLED() {
    if (!debug_config::kEnableStatusLight) {
        debug_logs::ledLogging("Status light is disabled, cannot start status LED module.");
        return false;
    }

    nicla::leds.begin();
    nicla::leds.setIntensity(led_config::kIntensity);
    nicla::leds.setColor(off);
    statusLedThread.enabled = true;

    debug_logs::ledLogging("Started status LED module.");
    return true;
}

bool setStatusState(BlinkState state) {
    // The state is tracked even with the light disabled, so inFailedState() still works.
    currentState = state;
    stepIndex = 0;
    stepStart = millis();
    debug_logs::ledLogging("%s: %s", currentPattern().name, currentPattern().message);

    if (!statusLedThread.enabled) return false;
    showCurrentStep();
    return true;
}

bool updateStatusLED() {
    runIfDue(statusLedThread);
    return statusLedThread.enabled;
}

bool inFailedState() {
    return currentPattern().failed;
}
/** @} */ // end of Public
