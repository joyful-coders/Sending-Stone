/**
 * @file event_format.h
 * @headerfile event_format.h "src/recorder/event_format.h"
 *
 * @brief Byte format of a recorded event, as stored on flash and sent over BLE.
 *
 * @details
 * An event transfers as one byte stream: a 64-byte @c EventMeta, then the
 * event's segment files back to back. Each segment is a sequence of records,
 * each a @c RecordHeader followed by its payload. Everything is packed and
 * little-endian. See BLE_PROTOCOL.md for the client-side decoding guide.
 *
 * @note
 * Any change here must bump @c kEventFormatVersion and update BLE_PROTOCOL.md
 * and the Python client.
 *
 */

#pragma once

#include <cstdint>

/** @brief Version of the event byte format, stored in @c EventMeta::formatVersion. */
constexpr uint8_t kEventFormatVersion = 1;

/** @brief What started an event. */
enum class TriggerType : uint8_t {
    /** @brief No trigger (unused). */
    None = 0,
    /** @brief The NDP recognized a keyword. Detail is the model's class index. */
    Keyword = 1,
    /** @brief A sudden jolt of movement. Detail is 1 (acceleration) or 2 (rotation). */
    Jolt = 2,
    /** @brief A manual trigger: the client's TRIGGER command. */
    Manual = 3,
    /** @brief The wearer pressed the button (trigger_config::kButtonPin). */
    Button = 4,
};

/** @brief Header at the start of every event (the transfer's first 64 bytes, file @c m.bin). */
struct __attribute__((packed)) EventMeta {
    /** @brief Always "NVEV". */
    char magic[4];
    /** @brief @c kEventFormatVersion. */
    uint8_t formatVersion;
    /** @brief @c TriggerType of the first trigger. */
    uint8_t triggerType;
    /** @brief Trigger detail, see @c TriggerType. */
    uint8_t triggerDetail;
    /** @brief 1 once recording finished and the sizes below are final, else 0. */
    uint8_t complete;
    /** @brief Event id, 1-65535. */
    uint16_t eventId;
    /** @brief Number of segment files that follow. */
    uint16_t segmentCount;
    /** @brief Device uptime, in ms, of the first trigger. */
    uint32_t triggerTimeMs;
    /** @brief Device uptime, in ms, of the last trigger (later triggers extend the event). */
    uint32_t lastTriggerTimeMs;
    /** @brief Device uptime, in ms, when recording stopped. 0 if the device lost power mid-event. */
    uint32_t endTimeMs;
    /** @brief Total bytes of segment data after this header. */
    uint32_t dataBytes;
    /** @brief First trigger's strength: peak g (jolt by acceleration), peak dps (jolt by rotation), else 0. */
    float triggerMagnitude;
    /** @brief Audio sample rate, Hz. */
    uint16_t audioRateHz;
    /** @brief Motion sample rate, Hz. */
    uint16_t motionRateHz;
    /** @brief Accelerometer full scale, g: counts * range / 32768 = g. */
    uint16_t accelRangeG;
    /** @brief Gyroscope full scale, dps: counts * range / 32768 = dps. */
    uint16_t gyroRangeDps;
    /** @brief Number of triggers during the event, including the first. */
    uint16_t triggerCount;
    /** @brief Keyword label of the first trigger (e.g. "NN0:alexa"), or the trigger name, null terminated. */
    char label[18];
    /** @brief Reserved, zero. */
    uint8_t reserved[4];
};
static_assert(sizeof(EventMeta) == 64, "EventMeta must stay 64 bytes, see BLE_PROTOCOL.md.");

/** @brief Record types within a segment. */
enum class RecordType : uint8_t {
    /** @brief A chunk of IMA-ADPCM audio, see @c AudioRecordHeader. */
    Audio = 1,
    /** @brief A block of motion samples, see @c MotionSample. */
    Motion = 2,
    /** @brief A trigger happened here, see @c MarkRecord. */
    Mark = 3,
};

/** @brief Header of every record in a segment. */
struct __attribute__((packed)) RecordHeader {
    /** @brief @c RecordType. */
    uint8_t type;
    /** @brief Audio: chunks missed just before this one. Motion: sample count. Mark: @c TriggerType. */
    uint8_t info;
    /** @brief Bytes of payload after this header. */
    uint16_t payloadBytes;
    /** @brief Device uptime, in ms, at the record's first sample. */
    uint32_t timestampMs;
};
static_assert(sizeof(RecordHeader) == 8, "RecordHeader must stay 8 bytes.");

/** @brief Start of an audio record's payload; @c (sampleCount + 1) / 2 ADPCM bytes follow, low nibble first. */
struct __attribute__((packed)) AudioRecordHeader {
    /** @brief IMA-ADPCM decoder predictor before the first sample. */
    int16_t predictor;
    /** @brief IMA-ADPCM step index (0-88) before the first sample. */
    uint8_t stepIndex;
    /** @brief Reserved, zero. */
    uint8_t reserved;
    /** @brief Samples encoded in this record. */
    uint16_t sampleCount;
};
static_assert(sizeof(AudioRecordHeader) == 6, "AudioRecordHeader must stay 6 bytes.");

/** @brief One motion sample in a motion record's payload. */
struct __attribute__((packed)) MotionSample {
    /** @brief Milliseconds after the record's timestamp. */
    uint16_t offsetMs;
    /** @brief Acceleration X/Y/Z, raw counts (see @c EventMeta::accelRangeG). */
    int16_t accel[3];
    /** @brief Angular rate X/Y/Z, raw counts (see @c EventMeta::gyroRangeDps). */
    int16_t gyro[3];
};
static_assert(sizeof(MotionSample) == 14, "MotionSample must stay 14 bytes.");

/** @brief Payload of a mark record (a trigger at this point in the recording). */
struct __attribute__((packed)) MarkRecord {
    /** @brief Trigger detail, see @c TriggerType. */
    uint8_t detail;
    /** @brief Reserved, zero. */
    uint8_t reserved[3];
    /** @brief Trigger strength, see @c EventMeta::triggerMagnitude. */
    float magnitude;
};
static_assert(sizeof(MarkRecord) == 8, "MarkRecord must stay 8 bytes.");
