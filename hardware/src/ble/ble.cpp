/**
 * @file ble.cpp
 *
 * @brief Implementation of the BLE sensor streaming module.
 *
 * @details
 * Runs a NimBLE GATT server exposing one service with a readings
 * characteristic and a version characteristic. A cooperative @c Thread packs
 * the IMU and sound modules' latest readings into a @c SensorPacket (see
 * ble_protocol.h) and notifies it to connected clients every @c
 * ble_config::kNotifyIntervalMs.
 *
 * NimBLE's connection callbacks run on its own host task, not the main loop,
 * so they only bump atomic counters, and the logging they trigger happens
 * later from @c updateBLEModule() on the main loop where the log queue is
 * safe to use.
 *
 */

#include <Arduino.h>
#include <Thread.h>
#include <NimBLEDevice.h>
#include <atomic>

#include "ble.h"
#include "ble_protocol.h"
#include "configs.h"
#include "logger.h"
#include "imu/imu.h"
#include "sound/sound.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the BLE module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief Timestamp for adding debug logs for the BLE loop */
unsigned long nowLoop = 0;

/** @brief Whether @c startBLEModule() succeeded. */
bool started = false;
/** @brief The NimBLE GATT server, owned by NimBLE. */
NimBLEServer* server = nullptr;
/** @brief The readings characteristic each @c SensorPacket is notified on, owned by NimBLE. */
NimBLECharacteristic* readingsChar = nullptr;
/** @brief Number of packets notified since boot. */
uint32_t packetsSent = 0;

/** @brief Connects seen by the host task that the main loop has not logged yet. */
std::atomic<uint8_t> pendingConnects{0};
/** @brief Disconnects seen by the host task that the main loop has not logged yet. */
std::atomic<uint8_t> pendingDisconnects{0};
/** @brief HCI reason code of the most recent disconnect. */
std::atomic<int> lastDisconnectReason{0};

/** @brief Records connection changes from NimBLE's host task for the main loop to log. */
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        pendingConnects++;
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        lastDisconnectReason = reason;
        pendingDisconnects++;
    }
};

/** @brief Server callback instance, static so NimBLE never needs to delete it. */
ServerCallbacks serverCallbacks;

/**
 * @brief Pack the latest IMU and sound readings into a @c SensorPacket.
 *
 * @param packet Destination that receives the encoded packet.
 *
 * @return Whether a packet was built.
 * @retval true @p packet holds the newest readings.
 * @retval false The IMU or sound module has no reading yet.
 *
 */
bool buildPacket(SensorPacket& packet) {
    ImuReading imu;
    SoundReading sound;
    if (!getLatestImuReading(imu) || !getLatestSoundReading(sound)) return false;

    packet.timestampMs = millis();
    for (uint8_t i = 0; i < 3; ++i) {
        packet.accel[i] = ble_protocol::encodeScaled(imu.accel[i], ble_protocol::kAccelScale);
        packet.gyro[i] = ble_protocol::encodeScaled(imu.gyro[i], ble_protocol::kGyroScale);
    }
    packet.temperature = ble_protocol::encodeScaled(imu.temperatureC, ble_protocol::kTempScale);
    packet.soundLevel = sound.level;
    return true;
}

/**
 * @brief One cooperative thread tick, notifying the newest packet to connected clients.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void bleTick() {
    if (server->getConnectedCount() == 0) return;

    SensorPacket packet;
    if (!buildPacket(packet)) return;

    readingsChar->setValue(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet));
    if (readingsChar->notify()) packetsSent++;
}

/** @brief Thread for notifying sensor packets. */
Thread bleThread = Thread([]() {
    bleTick();
});

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the BLE module, declared in ble.h.
 * @{
 */
bool startBLEModule() {
    if (!NimBLEDevice::init(ble_config::kDeviceName)) {
        debug_logs::bleLogging("Failed to initialize NimBLE.");
        return false;
    }
    NimBLEDevice::setPower(ble_config::kTxPowerDbm);

    server = NimBLEDevice::createServer();
    if (server == nullptr) {
        debug_logs::bleLogging("Failed to create GATT server.");
        return false;
    }
    server->setCallbacks(&serverCallbacks, false);
    server->advertiseOnDisconnect(true);

    NimBLEService* service = server->createService(ble_config::kServiceUuid);
    readingsChar = service->createCharacteristic(ble_config::kReadingsCharUuid,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, sizeof(SensorPacket));
    NimBLECharacteristic* versionChar = service->createCharacteristic(ble_config::kVersionCharUuid,
        NIMBLE_PROPERTY::READ, sizeof(BLE_PACKET_VERSION));

    // Seed both characteristics so a READ before the first notify returns a well-formed value.
    SensorPacket emptyPacket = {};
    readingsChar->setValue(reinterpret_cast<const uint8_t*>(&emptyPacket), sizeof(emptyPacket));
    versionChar->setValue(&BLE_PACKET_VERSION, sizeof(BLE_PACKET_VERSION));

    // The 128-bit service UUID and the name don't both fit one 31-byte
    // advertisement, so the name goes in the scan response.
    NimBLEAdvertisementData advData;
    advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    advData.addServiceUUID(NimBLEUUID(ble_config::kServiceUuid));
    NimBLEAdvertisementData scanData;
    scanData.setName(ble_config::kDeviceName);

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->setAdvertisementData(advData);
    advertising->setScanResponseData(scanData);
    advertising->enableScanResponse(true);
    if (!advertising->start()) {
        debug_logs::bleLogging("Failed to start advertising.");
        return false;
    }

    bleThread.setInterval(ble_config::kNotifyIntervalMs);
    started = true;

    debug_logs::bleLogging("Started BLE module, advertising as \"%s\" (packet v%u, %u bytes every %u ms).",
        ble_config::kDeviceName, BLE_PACKET_VERSION, sizeof(SensorPacket), ble_config::kNotifyIntervalMs);
    return true;
}

void updateBLEModule() {
    if (!started) return;

    if (bleThread.shouldRun()) bleThread.run();

    for (uint8_t n = pendingConnects.exchange(0); n > 0; --n) {
        debug_logs::bleLogging("Client connected.");
    }
    for (uint8_t n = pendingDisconnects.exchange(0); n > 0; --n) {
        debug_logs::bleLogging("Client disconnected (reason 0x%X), advertising again.", lastDisconnectReason.load());
    }

    if (millis() - nowLoop >= debug_config::kBLELoopDelay) {
        debug_logs::bleLogging("%u client(s) connected, %lu packets sent.", server->getConnectedCount(), packetsSent);
        nowLoop = millis();
    }
}
/** @} */ // end of Public
