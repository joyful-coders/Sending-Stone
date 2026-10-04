/**
 * @file ble_protocol.h
 * @headerfile ble_protocol.h "src/ble/ble_protocol.h"
 *
 * @brief Commands and messages of the BLE event transfer protocol.
 *
 * @details
 * This is the contract shared by @c ble.cpp and any client. The device
 * records continuously and only sends data after a trigger, as stored events
 * (see event_format.h for an event's bytes):
 *
 * - The client writes commands (first byte @c Command) to the control
 *   characteristic.
 * - The device notifies messages (first byte @c Message) on the events
 *   characteristic: an event starting or finishing, and replies to commands.
 * - During a transfer, the device notifies event bytes on the data
 *   characteristic, each notification a @c DataChunkHeader plus payload.
 *
 * Everything is packed and little-endian. See BLE_PROTOCOL.md for the full
 * client guide.
 *
 * @note
 * Any change here must bump @c BLE_PROTOCOL_VERSION and update BLE_PROTOCOL.md
 * and the Python client.
 *
 * 
 */

#pragma once

#include <cstdint>

/** @brief Protocol version, readable from the version characteristic. 1 was the old live sensor stream. */
constexpr uint8_t BLE_PROTOCOL_VERSION = 2;

namespace ble_protocol {
/** @brief Commands the client writes to the control characteristic. */
enum class Command : uint8_t {
    /** @brief List stored events. Reply: an EVENT_INFO per event, then LIST_END. */
    List = 0x01,
    /** @brief Send an event: u16 id, u32 offset, u16 max payload per chunk. Reply: data chunks, then TRANSFER_DONE. */
    Get = 0x02,
    /** @brief Delete an event (after saving it): u16 id. Reply: DELETED or ERROR. */
    Delete = 0x03,
    /** @brief Stop the current transfer. No reply. */
    Cancel = 0x04,
    /** @brief Trigger an event manually. Reply: EVENT_STARTED (or nothing if one is already recording, it's extended). */
    Trigger = 0x05,
};

/** @brief Messages the device notifies on the events characteristic. */
enum class Message : uint8_t {
    /** @brief A trigger started an event: u16 id, u8 trigger type, u8 detail, u32 trigger time ms. */
    EventStarted = 0x81,
    /** @brief An event finished recording and can be fetched: u16 id, u32 size, u8 trigger type. */
    EventReady = 0x82,
    /** @brief Reply to LIST, one per event: u16 id, u32 size (0 while recording), u8 trigger type, u8 complete. */
    EventInfo = 0x83,
    /** @brief End of a LIST reply: u16 number of events. */
    ListEnd = 0x84,
    /** @brief A transfer finished: u16 id, u32 total size. */
    TransferDone = 0x85,
    /** @brief Reply to DELETE: u16 id. */
    Deleted = 0x86,
    /** @brief A command failed: u8 @c ErrorCode, u16 id (0 if not about an event). */
    Error = 0x87,
};

/** @brief Error codes in an ERROR message. */
enum class ErrorCode : uint8_t {
    /** @brief The command was malformed or unknown. */
    BadCommand = 1,
    /** @brief No such event, or it's still recording. */
    NoSuchEvent = 2,
    /** @brief Reading the event from flash failed. */
    StorageError = 3,
};

/** @brief Start of every data notification; the event's bytes follow. */
struct __attribute__((packed)) DataChunkHeader {
    /** @brief Event being transferred. */
    uint16_t eventId;
    /** @brief Position of the payload in the event's byte stream. */
    uint32_t offset;
};
static_assert(sizeof(DataChunkHeader) == 6, "DataChunkHeader must stay 6 bytes.");

} // namespace ble_protocol
