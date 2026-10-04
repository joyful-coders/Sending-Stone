/**
 * @headerfile led_handler.h "src/led/led_handler.h"
 *
 */

#pragma once

#include <cstdint>

/**
 * @brief Every status LED state, as X(name, color, failed, log message).
 *
 * @details
 * The single source of truth for the status LED: expanded here into @c
 * BlinkState, and in led_handler.cpp into the pattern table. Each state's
 * blink timing is the @c k<name>Pattern array in led_handler.cpp. @c failed
 * states make @c inFailedState() true. To add a state, add a line here and its
 * pattern there.
 *
 * | State     | LED                            |
 * |-----------|--------------------------------|
 * | Setup     | blue blink                     |
 * | NDPFail   | slow magenta blink             |
 * | IMUFail   | two red blinks                 |
 * | MagFail   | three red blinks               |
 * | SoundFail | slow yellow blink              |
 * | BLEFail   | long then short blue blink     |
 * | Idle      | short green blip every 2 s     |
 *
 */
#define BLINK_STATES(X)                                                   \
    X(Setup,     blue,    false, "Device is starting up...")              \
    X(NDPFail,   magenta, true,  "NDP120 failed to load its firmware.")   \
    X(IMUFail,   red,     true,  "BMI270 failed to initialize.")          \
    X(MagFail,   red,     true,  "BMM150 failed to initialize.")          \
    X(SoundFail, yellow,  true,  "Microphone failed to start.")           \
    X(BLEFail,   blue,    true,  "BLE failed to start.")                  \
    X(Idle,      green,   false, "Device is idle.")

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
 * Starts the onboard RGB LED driver (over I2C, so call after @c
 * nicla::begin()) and the module's cooperative thread. Call once during @c
 * setup(), before @c setStatusState().
 *
 * @par Parameters
 * None.
 *
 * @return The status of the LED module startup attempt.
 * @retval true The LED was initialized.
 * @retval false @c debug_config::kEnableStatusLight is false.
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
