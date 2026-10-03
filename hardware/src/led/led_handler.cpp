/**
 * @file led_handler.cpp
 *
 * @brief Implementation of the status LED handler.
 *
 * @details
 * Each @c BlinkState maps to an @c LedPatternRunner holding an on/off pattern
 * for the XIAO's onboard user LED, advanced by a cooperative @c Thread ticking
 * every @c led_config::kThreadRefreshIntervalMs.
 *
 */

#include <Arduino.h>
#include <Thread.h>
#include <cstddef>

#include "led_handler.h"
#include "configs.h"
#include "logger.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the led module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief A single step in an LED pattern. */
struct LedStep {
    /** @brief Whether the LED is on for this step. */
    bool on;
    /** @brief How long, in milliseconds, this step lasts. */
    uint16_t duration;
};

/**
 * @defgroup LEDPatterns
 * Predefined LED patterns for different device states.
 * @{
 */
constexpr LedStep kLedOff = {false, 0};

constexpr LedStep kSetupPattern[] = {
    {true, 250},
    {false, 250},
};

constexpr LedStep kIMUFailPattern[] = {
    {true, 100},
    {false, 100},
    {true, 100},
    {false, 700},
};

constexpr LedStep kSoundFailPattern[] = {
    {true, 100},
    {false, 100},
    {true, 100},
    {false, 100},
    {true, 100},
    {false, 700},
};

constexpr LedStep kIdlePattern[] = {
    {true, 50},
    {false, 1950},
};
/** @} */ // end of LEDPatterns

/** @brief The current state of the status LED */
BlinkState currentState = BlinkState::Idle;
/** @brief A counter for logging pattern updates */
uint8_t loggingCounter = 0;

/** @brief Playback position within a single LED pattern. */
struct PatternState {
    /** @brief millis() timestamp the current step started at. */
    unsigned long lastTime = 0;
    /** @brief Index of the step currently playing. */
    size_t index = 0;
};

/** @brief Non-owning view over a static array of @c LedStep. */
struct PatternView {
    /** @brief Pointer to the first step of the pattern. */
    const LedStep* values = nullptr;
    /** @brief Number of steps in the pattern. */
    size_t length = 0;
};

/**
 * @brief Wrap a static LedStep array in a PatternView.
 *
 * @tparam N Length of the input array, deduced by the compiler.
 *
 * @param ledPattern Static array of @c LedStep to view.
 *
 * @return A @c PatternView over @p ledPattern.
 *
 */
template <size_t N>
constexpr PatternView makePatternView(const LedStep (&ledPattern)[N]) {
    return PatternView{ledPattern, N};
}

/** @brief Pairs an LED pattern with its playback state for one BlinkState. */
struct LedPatternRunner {
    /** @brief Pattern played on the status LED, defaults to steady off. */
    PatternView pattern = {&kLedOff, 1};
    /** @brief Message logged when this runner becomes active, or nullptr. */
    const char* logMessage = nullptr;
    /** @brief Maximum times @c logMessage may be logged, 0 means no limit. */
    uint8_t maxLogging = 0;
    /** @brief Playback position within @c pattern. */
    PatternState state = {};
    /** @brief Human readable name used in log messages. */
    const char* name = "Undefined";

    /**
     * @brief Restart the pattern from its first step.
     *
     * @param offsetMs Offset, in milliseconds, applied to the pattern's start
     * timestamp so a caller can stagger multiple runners.
     *
     * @par Returns
     * Nothing.
     */
    void resetPatternState(unsigned long offsetMs = 0) {
        state.lastTime = millis() - offsetMs;
        state.index = 0;
    }
};

/**
 * @defgroup LedPatternRunner Instances
 * Instances for managing different LED patterns
 * @{
 */
/** @brief Runner for BlinkState::Setup. */
LedPatternRunner setupRunner = {};
/** @brief Runner for BlinkState::IMUFail. */
LedPatternRunner imuFailRunner = {};
/** @brief Runner for BlinkState::SoundFail. */
LedPatternRunner soundFailRunner = {};
/** @brief Runner for BlinkState::Idle. */
LedPatternRunner idleRunner = {};
/** @} */ // end of LedPatternRunner Instances

/**
 * @brief Look up the LedPatternRunner for a given BlinkState.
 *
 * @param state State to look up.
 *
 * @return Pointer to the matching @c LedPatternRunner.
 * @retval nullptr No runner is defined for the given state.
 *
 */
LedPatternRunner* getRunnerByState(BlinkState state) {
    switch (state) {
        case BlinkState::Setup:
            return &setupRunner;
        case BlinkState::IMUFail:
            return &imuFailRunner;
        case BlinkState::SoundFail:
            return &soundFailRunner;
        case BlinkState::Idle:
            return &idleRunner;
        default:
            debug_logs::ledLogging("No LED pattern runner defined for the given state.");
            return nullptr;
    }
}

/**
 * @brief Drive the status LED to the state described by one LedStep.
 *
 * @param step Step describing the desired on/off state.
 *
 * @par Returns
 * Nothing.
 *
 */
void setLed(LedStep step) {
    digitalWrite(led_config::kStatusLedPin, (step.on != led_config::kActiveLow) ? HIGH : LOW);
}

/**
 * @brief Log a pattern's message, honoring its optional logging cap.
 *
 * @param logMessage Message to log, or nullptr to log nothing.
 * @param maxLogging Maximum times to log this message, 0 means no limit.
 *
 * @return Whether the message was logged.
 * @retval true The message was logged and @c loggingCounter was incremented.
 * @retval false @p logMessage was nullptr, or @p maxLogging was already reached.
 *
 */
bool threadPatternLogHelper(const char* logMessage, uint8_t maxLogging = 0) {
    if (logMessage != nullptr && (maxLogging == 0 || loggingCounter < maxLogging)) {
        debug_logs::ledLogging("%s", logMessage);
        loggingCounter++;
        return true;
    }
    return false;
}

/**
 * @brief Advance a runner's pattern by one tick.
 *
 * @details
 * Checks the pattern's current step against its elapsed time, and when the
 * step has run its full duration, drives the LED to the next step.
 *
 * @param runner Runner whose pattern and timing state should advance.
 *
 * @return Whether the runner had a valid pattern to advance.
 * @retval true The pattern was checked and advanced as needed.
 * @retval false @p runner's pattern is empty or unset.
 *
 */
bool threadPatternHelper(LedPatternRunner& runner) {
    unsigned long now = millis();

    if (runner.pattern.values == nullptr || runner.pattern.length == 0) return false;

    if (now - runner.state.lastTime >= runner.pattern.values[runner.state.index].duration) {
        runner.state.lastTime = now;
        setLed(runner.pattern.values[runner.state.index]);
        runner.state.index = (runner.state.index + 1) % runner.pattern.length;
    }
    threadPatternLogHelper(runner.logMessage, runner.maxLogging);

    return true;
}

/**
 * @brief Reset every LED runner's pattern state.
 *
 * @param offsetMs Offset, in milliseconds, forwarded to each runner's @c
 * resetPatternState().
 *
 * @par Returns
 * Nothing.
 */
void resetPatterns(unsigned long offsetMs = 0) {
    setupRunner.resetPatternState(offsetMs);
    imuFailRunner.resetPatternState(offsetMs);
    soundFailRunner.resetPatternState(offsetMs);
    idleRunner.resetPatternState(offsetMs);
}

/**
 * @brief Reset a single BlinkState's runner and log the change.
 *
 * @param state Blink state whose runner should be reset.
 * @param offsetMs Offset, in milliseconds, forwarded to the runner's
 * @c resetPatternState().
 *
 * @par Returns
 * Nothing.
 *
 */
void resetPatterns(BlinkState state, unsigned long offsetMs = 0) {
    LedPatternRunner* runner = getRunnerByState(state);
    if (runner == nullptr) return;
    runner->resetPatternState(offsetMs);
    debug_logs::ledLogging("Reset %s pattern state.", runner->name);
}

/**
 * @brief Thread callback that advances the current state's LED pattern.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void statusLedTick() {
    LedPatternRunner* runner = getRunnerByState(currentState);
    if (runner == nullptr) {
        // getRunnerByState() already logged the "no runner defined" message.
        return;
    }
    if (!threadPatternHelper(*runner)) {
        debug_logs::ledLogging("Error occurred while updating LED pattern for state %s.", runner->name);
    }
}

/** @brief The thread for managing the status LED updates. */
Thread statusLedThread = Thread([]() { statusLedTick(); });

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

    pinMode(led_config::kStatusLedPin, OUTPUT);
    setLed(kLedOff);

    // Initialize pattern runners with their respective patterns and log messages.
    setupRunner.pattern = makePatternView(kSetupPattern);
    setupRunner.logMessage = "Device is starting up...";
    setupRunner.maxLogging = 1;
    setupRunner.name = "Setup";

    imuFailRunner.pattern = makePatternView(kIMUFailPattern);
    imuFailRunner.logMessage = "MPU6050 failed to initialize.";
    imuFailRunner.maxLogging = 1;
    imuFailRunner.name = "IMUFail";

    soundFailRunner.pattern = makePatternView(kSoundFailPattern);
    soundFailRunner.logMessage = "Sound sensor failed to initialize.";
    soundFailRunner.maxLogging = 1;
    soundFailRunner.name = "SoundFail";

    idleRunner.pattern = makePatternView(kIdlePattern);
    idleRunner.logMessage = "Device is idle.";
    idleRunner.maxLogging = 1;
    idleRunner.name = "Idle";

    statusLedThread.setInterval(led_config::kThreadRefreshIntervalMs);

    resetPatterns();

    debug_logs::ledLogging("Started status LED module.");
    return true;
}

bool setStatusState(BlinkState state) {
    if (!debug_config::kEnableStatusLight) return false;

    currentState = state;
    loggingCounter = 0;
    resetPatterns(state);

    return true;
}

bool updateStatusLED() {
    if (!debug_config::kEnableStatusLight) return false;

    if (statusLedThread.shouldRun()) statusLedThread.run();
    return true;
}

bool inFailedState() {
    return
    currentState == BlinkState::IMUFail ||
    currentState == BlinkState::SoundFail;
}
/** @} */ // end of Public
