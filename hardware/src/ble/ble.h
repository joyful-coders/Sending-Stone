/**
 * @headerfile ble.h "src/ble/ble.h"
 *
 */

#pragma once

/**
 * @brief Start the BLE module, its GATT server, and advertising.
 *
 * @details
 * Initializes NimBLE as @c ble_config::kDeviceName, creates the sensor data
 * service with its readings (READ + NOTIFY) and version (READ)
 * characteristics, then starts advertising the service UUID so clients can
 * find the device. Advertising restarts automatically whenever a client
 * disconnects. See ble_protocol.h for the packet format.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the BLE module startup attempt.
 * @retval true The GATT server is up and the device is advertising.
 * @retval false NimBLE failed to initialize, or the server or advertising failed to start.
 *
 */
bool startBLEModule();

/**
 * @brief Run the BLE notify thread tick, if it is due.
 *
 * @details
 * Call regularly from the main loop. Once every @c
 * ble_config::kNotifyIntervalMs, while at least one client is connected, packs
 * the latest IMU and sound readings into one @c SensorPacket and notifies it on
 * the readings characteristic. Also logs connection changes and, every @c
 * debug_config::kBLELoopDelay, the connected client and sent packet counts.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateBLEModule();
