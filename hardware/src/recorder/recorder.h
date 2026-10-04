/**
 * @headerfile recorder.h "src/recorder/recorder.h"
 *
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "event_format.h"
#include "../imu/imu.h"

/** @brief Summary of one stored event, see @c listEvents(). */
struct EventInfo {
    /** @brief Event id. */
    uint16_t id;
    /** @brief Bytes in the event's transfer stream (64-byte header + segments). 0 while still recording. */
    uint32_t size;
    /** @brief What started the event. */
    TriggerType trigger;
    /** @brief Whether recording has finished (only complete events can be transferred). */
    bool complete;
};

/** @brief Called when a trigger starts a new event. */
typedef void (*EventStartedHandler)(uint16_t id, TriggerType trigger, uint8_t detail, uint32_t triggerTimeMs);
/** @brief Called when an event has finished recording and can be transferred. */
typedef void (*EventReadyHandler)(uint16_t id, uint32_t size, TriggerType trigger);

/**
 * @brief Start the recorder: mount the external flash and begin the rolling buffer.
 *
 * @details
 * Mounts the external flash's filesystem as @c recorder_config::kMountName
 * (the same filesystem the NDP firmware files live in, so call after @c
 * startNDPModule(), which unmounts it). Clears the old rolling buffer,
 * finishes any event left incomplete by a power loss, trims stored events to
 * @c recorder_config::kMaxEvents, then starts recording into the ring.
 *
 * @par Parameters
 * None.
 *
 * @return Whether the flash mounted and recording started.
 *
 */
bool startRecorder();

/**
 * @brief Rotate segments and finish events as their time comes.
 *
 * @details
 * Call every main loop iteration. Starts a new segment every @c
 * recorder_config::kSegmentMs, keeping the last @c
 * recorder_config::kPreTriggerMs in the ring, and ends an event once @c
 * recorder_config::kPostTriggerMs have passed since its last trigger.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateRecorder();

/**
 * @brief Record one audio chunk (an @c AudioChunkHandler, see audio.h).
 *
 * @param samples 16 kHz, 16-bit mono PCM samples.
 * @param count Number of samples.
 * @param missedChunks Chunks missed just before this one.
 * @param timestampMs millis() when the chunk was extracted.
 *
 * @par Returns
 * Nothing.
 *
 */
void recordAudioChunk(const int16_t* samples, size_t count, uint8_t missedChunks, uint32_t timestampMs);

/**
 * @brief Record one motion sample (an @c ImuSampleHandler, see imu.h).
 *
 * @param sample The sample.
 *
 * @par Returns
 * Nothing.
 *
 */
void recordMotionSample(const ImuSample& sample);

/**
 * @brief Start an event, or extend the one already recording.
 *
 * @details
 * A new event takes the rolling buffer's last ~10 s as its start and keeps
 * recording for @c recorder_config::kPostTriggerMs. A trigger during an event
 * extends it to that long after the new trigger, up to @c
 * recorder_config::kMaxEventMs in total. Either way a mark record is written
 * at this point in the recording.
 *
 * @param trigger What triggered.
 * @param detail Trigger detail, see @c TriggerType.
 * @param magnitude Trigger strength, see @c EventMeta::triggerMagnitude.
 * @param label Short description stored in the event header (e.g. the keyword label).
 *
 * @return The event's id, or 0 if the recorder isn't running or the event couldn't be created.
 *
 */
uint16_t triggerEvent(TriggerType trigger, uint8_t detail, float magnitude, const char* label);

/**
 * @brief Register the functions told when events start and finish.
 *
 * @param started Called when a trigger starts a new event (not on extensions).
 * @param ready Called when an event finishes recording.
 *
 * @par Returns
 * Nothing.
 *
 */
void setEventHandlers(EventStartedHandler started, EventReadyHandler ready);

/**
 * @brief List the stored events, oldest first.
 *
 * @param out Destination array.
 * @param maxEvents Capacity of @p out.
 *
 * @return Number of events written to @p out.
 *
 */
uint8_t listEvents(EventInfo* out, uint8_t maxEvents);

/**
 * @brief Delete a stored event.
 *
 * @param id Event to delete. The event currently recording can't be deleted.
 *
 * @return Whether the event existed and was deleted.
 *
 */
bool deleteEvent(uint16_t id);

/**
 * @brief Open a complete event for reading as one byte stream (header + segments).
 *
 * @details
 * Only one event can be open for reading at a time; opening another closes
 * the previous one.
 *
 * @param id Event to open.
 * @param totalBytes Receives the stream's total size.
 *
 * @return Whether the event exists, is complete, and was opened.
 *
 */
bool beginEventRead(uint16_t id, uint32_t& totalBytes);

/**
 * @brief Read from the open event's byte stream.
 *
 * @param offset Position in the stream.
 * @param buffer Destination.
 * @param maxBytes Most bytes to read.
 *
 * @return Bytes read (may be fewer than @p maxBytes at file boundaries), 0 at the end, or -1 on error.
 *
 */
int readEvent(uint32_t offset, uint8_t* buffer, size_t maxBytes);

/**
 * @brief Close the event opened by @c beginEventRead().
 *
 * @par Returns
 * Nothing.
 *
 */
void endEventRead();
