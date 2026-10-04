/**
 * @headerfile ble.h "src/ble/ble.h"
 *
 */

#pragma once

#include <cstdint>

#include "../recorder/event_format.h"

/**
 * @brief Start the BLE module, its GATT service, and advertising.
 *
 * @details
 * Starts ArduinoBLE as @c ble_config::kDeviceName with the service's version,
 * control, events, and data characteristics (see ble_protocol.h), then starts
 * advertising the service UUID so clients can find the device. Advertising
 * resumes automatically whenever a client disconnects.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the BLE module startup attempt.
 * @retval true The service is up and the device is advertising.
 * @retval false The BLE stack failed to start, or advertising failed to start.
 *
 */
bool startBLEModule();

/**
 * @brief Service the BLE stack and continue any event transfer.
 *
 * @details
 * Call every main loop iteration: runs @c BLE.poll() (which processes
 * connections and client commands) and sends up to @c
 * ble_config::kChunksPerLoop data chunks of the transfer in progress.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateBLEModule();

/**
 * @brief Tell the client an event started (an @c EventStartedHandler, see recorder.h).
 *
 * @par Returns
 * Nothing.
 *
 */
void notifyEventStarted(uint16_t id, TriggerType trigger, uint8_t detail, uint32_t triggerTimeMs);

/**
 * @brief Tell the client an event is ready to fetch (an @c EventReadyHandler, see recorder.h).
 *
 * @par Returns
 * Nothing.
 *
 */
void notifyEventReady(uint16_t id, uint32_t size, TriggerType trigger);
