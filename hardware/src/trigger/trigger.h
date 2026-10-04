/**
 * @headerfile trigger.h "src/trigger/trigger.h"
 *
 */

#pragma once

#include <cstdint>

/**
 * @brief Start the trigger module.
 *
 * @details
 * Registers the jolt detector for every IMU sample (if @c
 * trigger_config::kEnableJolt) and starts polling the NDP for keyword
 * matches (if @c trigger_config::kEnableKeyword). Each trigger calls @c
 * triggerEvent() on the recorder. Call after @c startIMUModule() and @c
 * startRecorder().
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void startTriggerModule();

/**
 * @brief Run the keyword polling thread tick, if it is due.
 *
 * @details
 * Call every main loop iteration (the same thread as all other NDP access).
 * Jolts are detected as IMU samples arrive, not here.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateTriggerModule();

/**
 * @brief Trigger an event manually (the client's TRIGGER command, and later a button).
 *
 * @return The event's id, or 0 if no event could be started.
 *
 */
uint16_t triggerManually();
