/**
 * @headerfile led_handler.h "src/led/led_handler.h"
 *
 */

#pragma once

#include <cstdint>

/**
 * @brief Every device state, as X(name, color, failed, log message).
 *
 * @details
 * The single source of truth for device state: expanded here into @c
 * BlinkState, and in led_handler.cpp into the state table. @c failed states
 * make @c inFailedState() true in every build. The LED itself (color, and the
 * @c k<name>Pattern blink timing in led_handler.cpp) only exists in debug
 * builds (@c NICLA_DEBUG = 1). To add a state, add a line here and its pattern
 * there.
 *
 * | State     | LED (debug builds)             |
 * |-----------|--------------------------------|
 * | Setup     | blue blink                     |
 * | NDPFail   | slow magenta blink             |
 * | IMUFail   | two red blinks                 |
 * | AudioFail | slow yellow blink              |
 * | StorageFail | slow cyan blink              |
 * | BLEFail   | long then short white blink    |
 * | Idle      | short green blip every 2 s     |
 *
 */
#define BLINK_STATES(X)                                                       \
    X(Setup,       blue,    false, "Device is starting up...")                \
    X(NDPFail,     magenta, true,  "NDP120 failed to load its firmware.")     \
    X(IMUFail,     red,     true,  "BMI270 failed to initialize.")            \
    X(AudioFail,   yellow,  true,  "Microphone failed to start.")             \
    X(StorageFail, cyan,    true,  "External flash storage failed to mount.") \
    X(BLEFail,     white,   true,  "BLE failed to start.")                    \
    X(Idle,        green,   false, "Device is idle.")

/** @brief Enumerates the possible states for the status LED, generated from @c BLINK_STATES. */
enum class BlinkState : uint8_t {
#define X(name, color, failed, message) name,
    BLINK_STATES(X)
#undef X
};

/**
 * @brief Start the status LED module.
 *
 * @details
 * In debug builds, starts the onboard RGB LED driver (over I2C, so call after
 * @c nicla::begin()) and the module's cooperative thread. Call once during @c
 * setup(), before @c setStatusState().
 *
 * @par Parameters
 * None.
 *
 * @return The status of the LED module startup attempt.
 * @retval true The LED was initialized.
 * @retval false This is a normal build, there is no status LED.
 *
 */
bool startStatusLED();

/**
 * @brief Switch the status LED to a new state's pattern.
 *
 * @details
 * Restarts the pattern from its first ("on") step immediately and queues the
 * state's log message once. The state is recorded even when the light is
 * disabled, so @c inFailedState() always reflects it.
 *
 * @param state The desired state for the status LED.
 *
 * @return Whether the LED itself was updated.
 * @retval true The LED now shows the new state.
 * @retval false The LED module is disabled, only the state was recorded.
 *
 */
bool setStatusState(BlinkState state);

/**
 * @brief Advance the status LED's pattern, if its thread is due.
 *
 * @details
 * Call regularly from the main loop. Checks the current step once per @c
 * led_config::kThreadRefreshIntervalMs, and writes the LED (over I2C) only
 * when a step changes.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the LED update attempt.
 * @retval true The LED module is running.
 * @retval false The LED module is disabled and is not running.
 *
 */
bool updateStatusLED();

/**
 * @brief Check if the status LED is in a failed state.
 *
 * @par Parameters
 * None.
 *
 * @return Whether the current state is marked failed in @c BLINK_STATES.
 *
 */
bool inFailedState();
