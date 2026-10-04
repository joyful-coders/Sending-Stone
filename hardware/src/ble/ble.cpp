/**
 * @file ble.cpp
 *
 * @brief Implementation of the BLE event transfer module.
 *
 * @details
 * Runs an ArduinoBLE peripheral with one service (see ble_protocol.h): the
 * client writes commands to the control characteristic, and the device
 * replies and announces events on the events characteristic and streams event
 * bytes on the data characteristic.
 *
 * ArduinoBLE delivers connection events and characteristic writes from inside
 * @c BLE.poll(), which runs on the main loop, so command handlers can use the
 * recorder directly. ArduinoBLE's notify busy-waits until the radio has a
 * free packet buffer, which on a slow link stalled the loop long enough to
 * drop audio. So a transfer only sends a chunk when a buffer is free (read
 * from the library's private counters). Each loop pass it keeps checking for
 * freed buffers for up to @c ble_config::kTransferBudgetUs, sending at most
 * @c ble_config::kChunksPerLoop chunks, then lets the loop continue.
 *
 * For speed, each connection is asked for long radio packets (Data Length
 * Extension, 251 bytes instead of 27, so a chunk fits in one packet instead
 * of ~10) and the 2M PHY (twice the bit rate). ArduinoBLE doesn't do either,
 * so the standard HCI commands are sent directly. Clients that can't do one
 * just keep the default. During a transfer the device also asks for a short
 * connection interval (more packets per second), and for a relaxed one again
 * when it ends, to save power while idle.
 *
 */

#include <Arduino.h>
#include <ArduinoBLE.h>
#include <utility/HCI.h>
#include <utility/ATT.h>
#include <string.h>

#include "ble.h"
#include "ble_protocol.h"
#include "../configs.h"
#include "../logger.h"
#include "../module_utils.h"
#include "../recorder/recorder.h"
#include "../trigger/trigger.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the BLE module.
 * These are not intended to be used outside of this module.
 * @{
 */
/** @brief Tag naming the private @c HCIClass::_pendingPkt (packets the radio hasn't sent yet), see module_utils.h. */
struct HciPendingTag {
    typedef uint8_t HCIClass::*type;
    friend type get(HciPendingTag);
};

/** @brief Tag naming the private @c BLEDevice::_addressType, see module_utils.h. */
struct DeviceAddressTypeTag {
    typedef uint8_t BLEDevice::*type;
    friend type get(DeviceAddressTypeTag);
};
/** @brief Tag naming the private @c BLEDevice::_address, see module_utils.h. */
struct DeviceAddressTag {
    typedef uint8_t (BLEDevice::*type)[6];
    friend type get(DeviceAddressTag);
};
/** @brief Tag naming the private @c HCIClass::_maxPkt (the radio's packet buffers), see module_utils.h. */
struct HciMaxTag {
    typedef uint8_t HCIClass::*type;
    friend type get(HciMaxTag);
};

namespace {
using namespace ble_protocol;

/** @brief Number of currently connected clients. */
uint8_t connectedCount = 0;
/** @brief The current connection's handle, or 0xFFFF. */
uint16_t linkHandle = 0xFFFF;
/** @brief Whether the fast-link request for @c linkHandle is still to be sent (from the main loop, not the connect callback). */
bool fastLinkPending = false;
/** @brief Whether the short (transfer) connection interval was last requested. */
bool fastIntervalRequested = false;

/** @brief HCI opcode of an LE controller command. */
constexpr uint16_t leCommand(uint16_t ocf) { return static_cast<uint16_t>((0x08 << 10) | ocf); }
/** @brief Longest radio packet payload with Data Length Extension, in bytes. */
constexpr uint16_t kMaxLinkOctets = 251;
/** @brief Airtime of the longest packet on the 1M PHY, in microseconds (the spec's value for 251 bytes). */
constexpr uint16_t kMaxLinkTimeUs = 2120;
/** @brief PHY bit masks: 1M and 2M. */
constexpr uint8_t kPhy1M = 0x01, kPhy2M = 0x02;
/** @brief Whether @c startBLEModule() succeeded. */
bool started = false;

/** @brief The device's service. */
BLEService service(ble_config::kServiceUuid);
/** @brief Protocol version, see @c BLE_PROTOCOL_VERSION. */
BLEUnsignedCharCharacteristic versionChar(ble_config::kVersionCharUuid, BLERead);
/** @brief Commands from the client. */
BLECharacteristic controlChar(ble_config::kControlCharUuid, BLEWrite | BLEWriteWithoutResponse, 16);
/** @brief Event messages and command replies to the client. */
BLECharacteristic eventsChar(ble_config::kEventsCharUuid, BLERead | BLENotify, 20);
/** @brief Event bytes during a transfer. */
BLECharacteristic dataChar(ble_config::kDataCharUuid, BLENotify, ble_config::kMaxDataNotifyBytes);

/**
 * @defgroup Transfer
 * The event transfer in progress, if any.
 * @{
 */
bool transferring = false;
uint16_t transferId = 0;
uint32_t transferOffset = 0;
uint32_t transferTotal = 0;
uint16_t transferChunkBytes = 0;
uint8_t chunkBuffer[ble_config::kMaxDataNotifyBytes];
/** @} */

/** @brief Read a little-endian u16 from @p p. */
uint16_t readU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
/** @brief Read a little-endian u32 from @p p. */
uint32_t readU32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
/** @brief Write a little-endian u16 to @p p. */
void writeU16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
/** @brief Write a little-endian u32 to @p p. */
void writeU32(uint8_t* p, uint32_t v) { for (uint8_t i = 0; i < 4; ++i) p[i] = (v >> (8 * i)) & 0xFF; }

/**
 * @brief Notify one message on the events characteristic.
 *
 * @param message The message bytes, starting with its @c Message code.
 * @param length Message length.
 *
 * @par Returns
 * Nothing.
 *
 */
void sendMessage(const uint8_t* message, uint8_t length) {
    eventsChar.writeValue(message, length);
}

/** @brief Send an ERROR message. */
void sendError(ErrorCode code, uint16_t id) {
    uint8_t msg[4] = {static_cast<uint8_t>(Message::Error), static_cast<uint8_t>(code)};
    writeU16(&msg[2], id);
    sendMessage(msg, sizeof(msg));
}

/** @brief Stop the transfer in progress, if any. */
void stopTransfer() {
    if (!transferring) return;
    transferring = false;
    endEventRead();
}

/** @brief Handle LIST: one EVENT_INFO per stored event, then LIST_END. */
void handleList() {
    EventInfo events[recorder_config::kMaxEvents + 1];
    uint8_t count = listEvents(events, recorder_config::kMaxEvents + 1);
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t msg[9] = {static_cast<uint8_t>(Message::EventInfo)};
        writeU16(&msg[1], events[i].id);
        writeU32(&msg[3], events[i].size);
        msg[7] = static_cast<uint8_t>(events[i].trigger);
        msg[8] = events[i].complete ? 1 : 0;
        sendMessage(msg, sizeof(msg));
    }
    uint8_t end[3] = {static_cast<uint8_t>(Message::ListEnd)};
    writeU16(&end[1], count);
    sendMessage(end, sizeof(end));
}

/** @brief Handle GET: open the event and start streaming it from the requested offset. */
void handleGet(uint16_t id, uint32_t offset, uint16_t maxPayload) {
    stopTransfer();
    uint32_t total = 0;
    if (!beginEventRead(id, total)) {
        sendError(ErrorCode::NoSuchEvent, id);
        return;
    }
    uint16_t largest = ble_config::kMaxDataNotifyBytes - sizeof(DataChunkHeader);
    transferChunkBytes = (maxPayload == 0 || maxPayload > largest) ? largest : maxPayload;
    transferId = id;
    transferOffset = offset < total ? offset : total;
    transferTotal = total;
    transferring = true;
    debug_logs::bleLogging("Sending event %u from %lu of %lu bytes, %u per chunk.", id,
        static_cast<unsigned long>(transferOffset), static_cast<unsigned long>(total), transferChunkBytes);
}

/** @brief Handle DELETE. */
void handleDelete(uint16_t id) {
    if (transferring && transferId == id) stopTransfer();
    if (!deleteEvent(id)) {
        sendError(ErrorCode::NoSuchEvent, id);
        return;
    }
    uint8_t msg[3] = {static_cast<uint8_t>(Message::Deleted)};
    writeU16(&msg[1], id);
    sendMessage(msg, sizeof(msg));
    debug_logs::bleLogging("Deleted event %u.", id);
}

/**
 * @brief Run a command the client wrote to the control characteristic.
 *
 * @param central The client.
 * @param characteristic The control characteristic.
 *
 * @par Returns
 * Nothing.
 *
 */
void onControlWritten(BLEDevice central, BLECharacteristic characteristic) {
    const uint8_t* cmd = controlChar.value();
    int length = controlChar.valueLength();
    if (length < 1) return;

    switch (static_cast<Command>(cmd[0])) {
        case Command::List:
            handleList();
            break;
        case Command::Get:
            if (length < 9) { sendError(ErrorCode::BadCommand, 0); break; }
            handleGet(readU16(&cmd[1]), readU32(&cmd[3]), readU16(&cmd[7]));
            break;
        case Command::Delete:
            if (length < 3) { sendError(ErrorCode::BadCommand, 0); break; }
            handleDelete(readU16(&cmd[1]));
            break;
        case Command::Cancel:
            stopTransfer();
            break;
        case Command::Trigger:
            triggerManually();
            break;
        default:
            sendError(ErrorCode::BadCommand, 0);
            break;
    }
}

/**
 * @brief Whether a notification can be sent without waiting for the radio.
 *
 * @return Whether the radio has a free packet buffer.
 *
 */
bool radioHasFreeBuffer() {
    return HCI.*get(HciPendingTag()) < HCI.*get(HciMaxTag());
}

/** @brief Send the next chunks of the transfer in progress. */
void continueTransfer() {
    if (!transferring) return;
    if (connectedCount == 0 || !dataChar.subscribed()) {
        stopTransfer();
        return;
    }

    uint32_t start = micros();
    for (uint8_t i = 0; i < ble_config::kChunksPerLoop && transferOffset < transferTotal; ++i) {
        // Sending without a free buffer would block, so wait for one only within the budget.
        while (!radioHasFreeBuffer()) {
            if (micros() - start >= ble_config::kTransferBudgetUs) return;
            HCI.poll(); // processes the radio's "packets sent" reports, freeing buffers
            // poll() can also run a client command (CANCEL, DELETE) or a disconnect.
            if (!transferring || connectedCount == 0) return;
        }
        DataChunkHeader header = {transferId, transferOffset};
        memcpy(chunkBuffer, &header, sizeof(header));
        int got = readEvent(transferOffset, chunkBuffer + sizeof(header), transferChunkBytes);
        if (got <= 0) {
            sendError(ErrorCode::StorageError, transferId);
            stopTransfer();
            return;
        }
        dataChar.writeValue(chunkBuffer, sizeof(header) + got);
        transferOffset += got;
    }

    if (transferOffset >= transferTotal) {
        uint8_t msg[7] = {static_cast<uint8_t>(Message::TransferDone)};
        writeU16(&msg[1], transferId);
        writeU32(&msg[3], transferTotal);
        sendMessage(msg, sizeof(msg));
        debug_logs::bleLogging("Sent event %u.", transferId);
        stopTransfer();
    }
}

/** @brief Log a new connection, and queue a fast-link request for it. */
void onConnected(BLEDevice central) {
    connectedCount++;
    linkHandle = ATT.connectionHandle(central.*get(DeviceAddressTypeTag()), central.*get(DeviceAddressTag()));
    fastLinkPending = linkHandle != 0xFFFF;
    fastIntervalRequested = false;
    debug_logs::bleLogging("Client %s connected.", central.address().c_str());
}

/**
 * @brief Make new connections allow long packets and the 2M PHY.
 *
 * @details
 * The controller then accepts them when the client proposes them, and uses
 * them for the per-connection requests in @c requestFastLink().
 *
 * @par Returns
 * Nothing.
 *
 */
void setFastLinkDefaults() {
    uint16_t dataLength[2] = {kMaxLinkOctets, kMaxLinkTimeUs};
    int lengthStatus = HCI.sendCommand(leCommand(0x0024), sizeof(dataLength), dataLength); // LE Write Suggested Default Data Length
    uint8_t phy[3] = {0x00, kPhy1M | kPhy2M, kPhy1M | kPhy2M}; // allow both PHYs, both directions
    int phyStatus = HCI.sendCommand(leCommand(0x0031), sizeof(phy), phy);                 // LE Set Default PHY
    debug_logs::bleLogging("Link defaults: long packets %s, 2M PHY %s.", lengthStatus == 0 ? "on" : "unsupported",
                           phyStatus == 0 ? "allowed" : "unsupported");
}

/**
 * @brief Ask the client for long packets and the 2M PHY on one connection.
 *
 * @param handle The connection.
 *
 * @par Returns
 * Nothing.
 *
 */
void requestFastLink(uint16_t handle) {
    struct __attribute__((packed)) {
        uint16_t handle, octets, timeUs;
    } length = {handle, kMaxLinkOctets, kMaxLinkTimeUs};
    int lengthStatus = HCI.sendCommand(leCommand(0x0022), sizeof(length), &length); // LE Set Data Length

    struct __attribute__((packed)) {
        uint16_t handle;
        uint8_t allPhys, txPhys, rxPhys;
        uint16_t options;
    } phy = {handle, 0x00, kPhy2M, kPhy2M, 0};
    int phyStatus = HCI.sendCommand(leCommand(0x0032), sizeof(phy), &phy); // LE Set PHY (the client may decline)
    debug_logs::bleLogging("Fast link requested: long packets %d, 2M PHY %d (0 = ok).", lengthStatus, phyStatus);
}

/** @brief Log a disconnection and drop any transfer. ArduinoBLE resumes advertising on its own. */
void onDisconnected(BLEDevice central) {
    if (connectedCount > 0) connectedCount--;
    if (connectedCount == 0) linkHandle = 0xFFFF;
    fastLinkPending = false;
    stopTransfer();
    debug_logs::bleLogging("Client %s disconnected.", central.address().c_str());
}

/** @brief Periodic status log. */
void bleLogTick() {
    debug_logs::bleLogging("%u client(s) connected%s.", connectedCount, transferring ? ", transferring" : "");
}

/** @brief Thread for periodically logging connection status. */
Thread bleLogThread = makeIdleThread(bleLogTick, debug_config::kBLELoopDelay);

/**
 * @brief Ask the client for the transfer or the idle connection interval.
 *
 * @details
 * Tries the controller's connection update first; if the client doesn't
 * support that procedure, sends the L2CAP request every client must handle.
 *
 * @param fast Whether to ask for the short (transfer) interval.
 *
 * @par Returns
 * Nothing.
 *
 */
void requestInterval(bool fast) {
    uint16_t minInterval = fast ? ble_config::kTransferIntervalMin : ble_config::kIdleIntervalMin;
    uint16_t maxInterval = fast ? ble_config::kTransferIntervalMax : ble_config::kIdleIntervalMax;
    int status = HCI.leConnUpdate(linkHandle, minInterval, maxInterval, 0, ble_config::kSupervisionTimeout);
    if (status != 0) {
        struct __attribute__((packed)) {
            uint8_t code, identifier;
            uint16_t length, minInterval, maxInterval, latency, timeout;
        } request = {0x12, 0x01, 8, minInterval, maxInterval, 0, ble_config::kSupervisionTimeout}; // Connection Parameter Update Request
        HCI.sendAclPkt(linkHandle, 0x0005, sizeof(request), &request); // L2CAP signaling channel
    }
    debug_logs::bleLogging("Requested %s interval %u-%u ms (%s).", fast ? "transfer" : "idle",
                           minInterval * 5 / 4, maxInterval * 5 / 4, status == 0 ? "link layer" : "L2CAP");
}

} // namespace
template struct PrivateAccess<DeviceAddressTypeTag, &BLEDevice::_addressType>;
template struct PrivateAccess<DeviceAddressTag, &BLEDevice::_address>;
template struct PrivateAccess<HciPendingTag, &HCIClass::_pendingPkt>;
template struct PrivateAccess<HciMaxTag, &HCIClass::_maxPkt>;
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

    setFastLinkDefaults();
    BLE.setLocalName(ble_config::kDeviceName);
    BLE.setDeviceName(ble_config::kDeviceName);
    BLE.setEventHandler(BLEConnected, onConnected);
    BLE.setEventHandler(BLEDisconnected, onDisconnected);
    controlChar.setEventHandler(BLEWritten, onControlWritten);

    service.addCharacteristic(versionChar);
    service.addCharacteristic(controlChar);
    service.addCharacteristic(eventsChar);
    service.addCharacteristic(dataChar);
    BLE.addService(service);
    BLE.setAdvertisedService(service);
    versionChar.writeValue(BLE_PROTOCOL_VERSION);

    if (!BLE.advertise()) {
        debug_logs::bleLogging("Failed to start advertising.");
        return false;
    }

    started = true;
    bleLogThread.enabled = true;
    debug_logs::bleLogging("Started BLE module, advertising as \"%s\" (protocol v%u).",
        ble_config::kDeviceName, BLE_PROTOCOL_VERSION);
    return true;
}

void updateBLEModule() {
    if (!started) return;
    BLE.poll();
    if (fastLinkPending) {
        requestFastLink(linkHandle);
        fastLinkPending = false;
    }
    if (linkHandle != 0xFFFF && transferring != fastIntervalRequested) {
        requestInterval(transferring);
        fastIntervalRequested = transferring;
    }
    continueTransfer();
    runIfDue(bleLogThread);
}

void notifyEventStarted(uint16_t id, TriggerType trigger, uint8_t detail, uint32_t triggerTimeMs) {
    uint8_t msg[9] = {static_cast<uint8_t>(Message::EventStarted)};
    writeU16(&msg[1], id);
    msg[3] = static_cast<uint8_t>(trigger);
    msg[4] = detail;
    writeU32(&msg[5], triggerTimeMs);
    sendMessage(msg, sizeof(msg));
}

void notifyEventReady(uint16_t id, uint32_t size, TriggerType trigger) {
    uint8_t msg[8] = {static_cast<uint8_t>(Message::EventReady)};
    writeU16(&msg[1], id);
    writeU32(&msg[3], size);
    msg[7] = static_cast<uint8_t>(trigger);
    sendMessage(msg, sizeof(msg));
}
/** @} */ // end of Public
