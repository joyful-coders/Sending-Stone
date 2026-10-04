/**
 * @headerfile ble.h "src/ble/ble.h"
 *
 */

#pragma once

/**
 * @brief Start the BLE module, its GATT service, and advertising.
 *
 * @details
 * Starts ArduinoBLE as @c ble_config::kDeviceName, adds the sensor data
 * service with its readings (READ + NOTIFY) and version (READ)
 * characteristics, then starts advertising the service UUID so clients can
 * find the device. Advertising resumes automatically whenever a client
 * disconnects. See ble_protocol.h for the packet format.
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
 * @brief Service the BLE stack and run the notify thread tick, if it is due.
 *
 * @details
 * Call every main loop iteration, since it also runs @c BLE.poll(), which
 * processes connection events. Once every @c ble_config::kNotifyIntervalMs,
 * while a client is subscribed, packs every sensor's latest reading into one
 * @c SensorPacket and notifies it on the readings characteristic. Also logs
 * connection changes and, every @c debug_config::kBLELoopDelay, the connected
 * client and sent packet counts.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateBLEModule();
