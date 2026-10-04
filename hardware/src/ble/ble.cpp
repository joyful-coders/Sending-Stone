/**
 * @file ble.cpp
 *
 * @brief Implementation of the BLE sensor streaming module.
 *
 * @details
 * Runs an ArduinoBLE peripheral exposing one service with a readings
 * characteristic and a version characteristic. A cooperative @c Thread packs
 * the IMU, magnetometer, sound, and battery modules' latest readings into a
 * @c SensorPacket (see ble_protocol.h) and notifies it to subscribed clients
 * every @c ble_config::kNotifyIntervalMs.
 *
 * ArduinoBLE delivers connection events from inside @c BLE.poll(), which runs
 * on the main loop, so the event handlers can log directly.
 *
 * @note
 * ArduinoBLE truncates notifications to the connection's ATT MTU minus 3. The
 * 30-byte packet needs an MTU of at least 33, which clients negotiate on
 * connect. Clients that don't still receive the first 20 bytes.
 *
 */

#include <Arduino.h>
#include <ArduinoBLE.h>

#include "ble.h"
#include "ble_protocol.h"
#include "../configs.h"
#include "../logger.h"
#include "../module_utils.h"
#include "../imu/imu.h"
#include "../mag/mag.h"
#include "../sound/sound.h"
#include "../battery/battery.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the BLE module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief Number of currently connected clients. */
uint8_t connectedCount = 0;
/** @brief Number of packets notified since boot. */
uint32_t packetsSent = 0;

/** @brief The sensor data service. */
BLEService sensorService(ble_config::kServiceUuid);
/** @brief The readings characteristic each @c SensorPacket is notified on, fixed length. */
BLECharacteristic readingsChar(ble_config::kReadingsCharUuid, BLERead | BLENotify, sizeof(SensorPacket), true);
/** @brief The packet version characteristic, carrying @c BLE_PACKET_VERSION. */
BLEUnsignedCharCharacteristic versionChar(ble_config::kVersionCharUuid, BLERead);

/**
 * @brief Log a new connection.
 *
 * @param central The client that connected.
 *
 * @par Returns
 * Nothing.
 *
 */
void onConnected(BLEDevice central) {
    connectedCount++;
    debug_logs::bleLogging("Client %s connected.", central.address().c_str());
}

/**
 * @brief Log a disconnection. ArduinoBLE resumes advertising on its own.
 *
 * @param central The client that disconnected.
 *
 * @par Returns
 * Nothing.
 *
 */
void onDisconnected(BLEDevice central) {
    if (connectedCount > 0) connectedCount--;
    debug_logs::bleLogging("Client %s disconnected.", central.address().c_str());
}

/**
 * @brief Pack every sensor module's latest reading into a @c SensorPacket.
 *
 * @details
 * The IMU is the packet's clock, so no packet is built until it has a sample.
 * Fields from modules without a reading yet are left at zero (battery percent
 * at -1).
 *
 * @param packet Destination that receives the encoded packet.
 *
 * @return Whether a packet was built.
 * @retval true @p packet holds the newest readings.
 * @retval false The IMU has no reading yet.
 *
 */
bool buildPacket(SensorPacket& packet) {
    ImuReading imu;
    if (!getLatestImuReading(imu)) return false;

    packet = {};
    packet.timestampMs = millis();
    for (uint8_t i = 0; i < 3; ++i) {
        packet.accel[i] = ble_protocol::encodeScaled(imu.accel[i], ble_protocol::kAccelScale);
        packet.gyro[i] = ble_protocol::encodeScaled(imu.gyro[i], ble_protocol::kGyroScale);
    }
    packet.temperature = ble_protocol::encodeScaled(imu.temperatureC, ble_protocol::kTempScale);

    SoundReading sound;
    if (getLatestSoundReading(sound)) packet.soundLevel = sound.level;

    MagReading mag;
    if (getLatestMagReading(mag)) {
        for (uint8_t i = 0; i < 3; ++i) {
            packet.mag[i] = ble_protocol::encodeScaled(mag.field[i], ble_protocol::kMagScale);
        }
    }

    BatteryReading battery;
    packet.batteryPercent = -1;
    if (getLatestBatteryReading(battery)) {
        packet.batteryMilliVolts = battery.milliVolts;
        packet.batteryPercent = battery.percent;
        if (battery.onBattery) packet.flags |= ble_protocol::kFlagOnBattery;
        if (battery.charging) packet.flags |= ble_protocol::kFlagCharging;
    }
    return true;
}

/**
 * @brief One cooperative thread tick, notifying the newest packet to subscribed clients.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void bleTick() {
    if (connectedCount == 0 || !readingsChar.subscribed()) return;

    SensorPacket packet;
    if (!buildPacket(packet)) return;

    if (readingsChar.writeValue(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet))) packetsSent++;
}

/**
 * @brief One logging tick, queuing the connection and packet counts.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void bleLogTick() {
    debug_logs::bleLogging("%u client(s) connected, %lu packets sent.", connectedCount, packetsSent);
}

/** @brief Thread for notifying sensor packets. */
Thread bleThread = makeIdleThread(bleTick, ble_config::kNotifyIntervalMs);
/** @brief Thread for periodically logging connection status. */
Thread bleLogThread = makeIdleThread(bleLogTick, debug_config::kBLELoopDelay);

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the BLE module, declared in ble.h.
 * @{
 */
bool startBLEModule() {
    if (!BLE.begin()) {
        debug_logs::bleLogging("Failed to start the BLE stack.");
        return false;
    }

    BLE.setLocalName(ble_config::kDeviceName);
    BLE.setDeviceName(ble_config::kDeviceName);
    BLE.setEventHandler(BLEConnected, onConnected);
    BLE.setEventHandler(BLEDisconnected, onDisconnected);

    sensorService.addCharacteristic(readingsChar);
    sensorService.addCharacteristic(versionChar);
    BLE.addService(sensorService);
    BLE.setAdvertisedService(sensorService);

    // Seed both characteristics so a READ before the first notify returns a well-formed value.
    SensorPacket emptyPacket = {};
    emptyPacket.batteryPercent = -1;
    readingsChar.writeValue(reinterpret_cast<const uint8_t*>(&emptyPacket), sizeof(emptyPacket));
    versionChar.writeValue(BLE_PACKET_VERSION);

    if (!BLE.advertise()) {
        debug_logs::bleLogging("Failed to start advertising.");
        return false;
    }

    bleThread.enabled = true;
    bleLogThread.enabled = true;

    debug_logs::bleLogging("Started BLE module, advertising as \"%s\" (packet v%u, %u bytes every %u ms).",
        ble_config::kDeviceName, BLE_PACKET_VERSION, sizeof(SensorPacket), ble_config::kNotifyIntervalMs);
    return true;
}

void updateBLEModule() {
    if (!bleThread.enabled) return; // not started

    BLE.poll();
    runIfDue(bleThread);
    runIfDue(bleLogThread);
}
/** @} */ // end of Public
