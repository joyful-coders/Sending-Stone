/**
 * @file led_handler.cpp
 *
 * @brief Implementation of the device state tracker and its debug status LED.
 *
 * @details
 * @c BLINK_STATES (led_handler.h) expands into a constant table kept in flash,
 * indexed by @c BlinkState. Every build tracks the current state, so @c
 * inFailedState() works everywhere. Debug builds (@c NICLA_DEBUG = 1) also
 * play each state's pattern on the Nicla's RGB LED (over I2C via @c
 * nicla::leds): only the current state plays, advanced by a cooperative @c
 * Thread every @c led_config::kThreadRefreshIntervalMs, and the LED is only
 * written when a step changes. Normal builds compile the LED out entirely.
 *
 */

#include <Arduino.h>

#include "led_handler.h"
#include "../configs.h"
#include "../logger.h"

#if NICLA_DEBUG
#include <Nicla_System.h>
#include "../module_utils.h"
#endif

/**
 * @defgroup Private
 * Member variables/functions used internally by the led module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief The current device state. */
BlinkState currentState = BlinkState::Idle;

/** @brief Whether each state counts as failed, in @c BlinkState order. */
constexpr bool kFailed[] = {
#define X(name, color, failed, message) failed,
    BLINK_STATES(X)
#undef X
};

#if NICLA_DEBUG
/**
 * @defgroup LEDPatterns
 * Blink timing for each @c BLINK_STATES entry: step durations in
 * milliseconds, alternating on, off, on, off... starting with "on".
 * @{
 */
constexpr uint16_t kSetupPattern[]     = {250, 250};
constexpr uint16_t kNDPFailPattern[]   = {500, 500};
constexpr uint16_t kIMUFailPattern[]   = {100, 100, 100, 700};
constexpr uint16_t kAudioFailPattern[] = {500, 500};
constexpr uint16_t kStorageFailPattern[] = {500, 500};
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
    /** @brief The state's name, for log messages. */
    const char* name;
    /** @brief Message logged when the state becomes active. */
    const char* message;
};

/** @brief Every state's pattern, in @c BlinkState order. */
constexpr LedPattern kPatterns[] = {
#define X(name, color, failed, message) \
    {k##name##Pattern, sizeof(k##name##Pattern) / sizeof(k##name##Pattern[0]), color, #name, message},
    BLINK_STATES(X)
#undef X
};

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
#endif // NICLA_DEBUG

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the led module, declared in led_handler.h.
 * @{
 */
bool startStatusLED() {
#if NICLA_DEBUG
    nicla::leds.begin();
    nicla::leds.setIntensity(led_config::kIntensity);
    nicla::leds.setColor(off);
    statusLedThread.enabled = true;

    debug_logs::ledLogging("Started status LED module.");
    return true;
#else
    return false;
#endif
}

bool setStatusState(BlinkState state) {
    currentState = state;

#if NICLA_DEBUG
    stepIndex = 0;
    stepStart = millis();
    debug_logs::ledLogging("%s: %s", currentPattern().name, currentPattern().message);

    if (!statusLedThread.enabled) return false;
    showCurrentStep();
    return true;
#else
    return false;
#endif
}

bool updateStatusLED() {
#if NICLA_DEBUG
    runIfDue(statusLedThread);
    return statusLedThread.enabled;
#else
    return false;
#endif
}

bool inFailedState() {
    return kFailed[static_cast<uint8_t>(currentState)];
}
/** @} */ // end of Public
