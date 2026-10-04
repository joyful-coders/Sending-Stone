/**
 * @file recorder.cpp
 *
 * @brief Implementation of the rolling buffer and event recorder.
 *
 * @details
 * Audio (IMA-ADPCM) and motion records are written continuously into
 * segment files of @c recorder_config::kSegmentMs on the external flash's
 * LittleFS (mounted as @c recorder_config::kMountName):
 *
 * - Normally, segments go to a ring of @c kRingSlots files in @c ring/, so
 *   only the last ~10 s is ever kept.
 * - On a trigger, the ring's files are renamed (a metadata-only move, no
 *   copying) into a new event directory @c ev/NNNNN/ as its first segments
 *   (@c s000.bin ...), and recording continues straight into that directory
 *   until @c recorder_config::kPostTriggerMs after the last trigger. The
 *   event's header is @c m.bin, written at the start and again (with final
 *   sizes) at the end.
 *
 * Events are deleted (by the client, or oldest first when there are too many
 * or free space drops below @c recorder_config::kReservedFlashBytes) in the
 * background, one file per updateRecorder() call, so recording never stalls.
 *
 * Records are written straight to the file with mbed's @c File API (no stdio
 * buffers, and no extra RAM buffer: LittleFS already caches each file's
 * writes in @c recorder_config::kFlashProgramBytes units). RAM is tight on
 * this chip, see README.md. Everything runs on the main loop, which is also the only thread that touches the NDP, so flash and NDP
 * traffic on the shared SPI bus never interleave.
 *
 * Byte formats are in event_format.h.
 *
 */

#include <Arduino.h>
#include <LittleFileSystem.h>
#include <File.h>
#include <Dir.h>
#include <errno.h>
#include <string.h>
#include <new>

#include "recorder.h"
#include "adpcm.h"
#include "nor_flash.h"
#include "../configs.h"
#include "../logger.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the recorder.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
using namespace recorder_config;

/** @brief Ring files: enough completed segments to cover the pre-trigger window, plus the one being written. */
constexpr uint8_t kRingSlots = static_cast<uint8_t>((kPreTriggerMs + kSegmentMs - 1) / kSegmentMs + 1);
/** @brief Most segment files one event can have (pre-trigger ring + the longest recording). */
constexpr uint16_t kMaxSegments = kRingSlots + static_cast<uint16_t>(kMaxEventMs / kSegmentMs) + 2;
/** @brief Largest audio record payload's sample count; longer chunks are split. */
constexpr size_t kMaxAudioSamplesPerRecord = 1024;
/** @brief Bytes in the event header that starts every transfer. */
constexpr uint32_t kMetaBytes = sizeof(EventMeta);
/** @brief Audio sample rate the NDP delivers. */
constexpr uint16_t kAudioRateHz = 16000;
/** @brief Size of path buffers: fits "ev/65535/s65535.bin" with room to spare. */
constexpr size_t kPathBytes = 32;

/**
 * @brief The external flash (same chip and pins as the NDP library's, which it releases after loading).
 *
 * @details
 * Our own driver rather than the core's SPIFBlockDevice, which is ~15x slower
 * at writing (see nor_flash.h).
 *
 */
NorFlash flash(SPI_PSELMOSI0, SPI_PSELMISO0, SPI_PSELSCK0, CS_FLASH, 16000000);
/**
 * @brief The flash's filesystem, mounted under @c kMountName.
 *
 * @details
 * The core's defaults except the program size (see @c kFlashProgramBytes).
 * The block size and lookahead must match the NDP library's mount of the same
 * flash, and they do.
 *
 */
mbed::LittleFileSystem fs(kMountName, nullptr, MBED_LFS_READ_SIZE, kFlashProgramBytes,
                          MBED_LFS_BLOCK_SIZE, MBED_LFS_LOOKAHEAD);
/** @brief Whether @c fs is mounted and recording is running. */
bool running = false;

/**
 * @defgroup SegmentWriter
 * The segment file currently being written, and its buffers.
 * @{
 */
mbed::File segment;
bool segmentOpen = false;
uint32_t segmentBytes = 0;
uint32_t segmentStartMs = 0;
bool segmentWriteFailed = false;
MotionSample motionBuffer[kMotionSamplesPerRecord];
uint8_t motionCount = 0;
uint32_t motionStartMs = 0;
adpcm::State adpcmState;
/** @} */

/**
 * @defgroup Ring
 * Rolling buffer bookkeeping: @c ringHead is the slot being written, the
 * @c ringCount slots before it hold completed segments (oldest first).
 * @{
 */
uint8_t ringHead = 0;
uint8_t ringCount = 0;
uint32_t ringBytes[kRingSlots] = {};
/** @} */

/**
 * @defgroup Capture
 * The event currently recording.
 * @{
 */
bool capturing = false;
EventMeta meta;
uint16_t segmentCount = 0;       ///< files in the event so far, including the open one
uint32_t eventDataBytes = 0;     ///< bytes in the event's completed segments
uint32_t eventStartMs = 0;
uint32_t captureEndMs = 0;
uint16_t nextEventId = 1;
EventStartedHandler startedHandler = nullptr;
EventReadyHandler readyHandler = nullptr;
/** @} */

/**
 * @defgroup Deletion
 * Events whose files are being removed in the background, one file per
 * updateRecorder() call: removing an event's ~30 files at once blocks for
 * ~1.5 s, long enough for the NDP's audio buffer to overflow.
 * @{
 */
constexpr uint8_t kMaxPendingDeletes = 4;
uint16_t pendingDeletes[kMaxPendingDeletes]; ///< oldest first
uint32_t pendingDeleteBytes[kMaxPendingDeletes]; ///< each one's size, freed once it's removed
uint8_t pendingDeleteCount = 0;
uint16_t pendingDeleteSegment = 0;            ///< next segment of pendingDeletes[0] to remove
/** @} */

/**
 * @defgroup Reader
 * The event open for transfer.
 * @{
 */
bool reading = false;
uint16_t readId = 0;
EventMeta readHeader;
mbed::File readFile;
int readFileIndex = -1;          ///< segment open in readFile, -1 if none
uint32_t readFileStart = 0;      ///< stream offset of that segment's first byte
uint32_t readFileBytes = 0;      ///< that segment's size
/** @} */


/** @brief Write "ring/r<slot>.bin" into @p out. */
void ringPath(uint8_t slot, char* out, size_t size) {
    snprintf(out, size, "ring/r%u.bin", slot);
}

/** @brief Write "ev/<id>" into @p out. */
void eventDirPath(uint16_t id, char* out, size_t size) {
    snprintf(out, size, "ev/%05u", id);
}

/** @brief Write "ev/<id>/m.bin" into @p out. */
void metaPath(uint16_t id, char* out, size_t size) {
    snprintf(out, size, "ev/%05u/m.bin", id);
}

/** @brief Write "ev/<id>/s<index>.bin" into @p out. */
void segmentPath(uint16_t id, uint16_t index, char* out, size_t size) {
    snprintf(out, size, "ev/%05u/s%03u.bin", id, index);
}

/**
 * @brief Write bytes to the open segment.
 *
 * @param data Bytes to write.
 * @param bytes Number of bytes.
 *
 * @par Returns
 * Nothing.
 *
 */
void writeSegment(const void* data, size_t bytes) {
    if (!segmentOpen || bytes == 0) return;
    ssize_t written = segment.write(data, bytes);
    if (written == static_cast<ssize_t>(bytes)) {
        segmentBytes += bytes;
    } else if (!segmentWriteFailed) {
        segmentWriteFailed = true; // log once per segment
        debug_logs::recorderLogging("Segment write failed (%d).", static_cast<int>(written));
    }
}

/**
 * @brief Write one record: its header, then up to two payload parts.
 *
 * @par Returns
 * Nothing.
 *
 */
void appendRecord(RecordType type, uint8_t info, uint32_t timestampMs,
                  const void* part1, size_t bytes1, const void* part2 = nullptr, size_t bytes2 = 0) {
    if (!segmentOpen) return;
    RecordHeader header = {static_cast<uint8_t>(type), info, static_cast<uint16_t>(bytes1 + bytes2), timestampMs};
    writeSegment(&header, sizeof(header));
    writeSegment(part1, bytes1);
    writeSegment(part2, bytes2);
}

/** @brief Write any buffered motion samples as one motion record. */
void flushMotion() {
    if (motionCount == 0) return;
    appendRecord(RecordType::Motion, motionCount, motionStartMs, motionBuffer, motionCount * sizeof(MotionSample));
    motionCount = 0;
}

/**
 * @brief Open (create or truncate) a segment file for writing.
 *
 * @param path Path within the filesystem.
 *
 * @return Whether it opened.
 *
 */
bool openSegment(const char* path) {
    int err = segment.open(&fs, path, O_WRONLY | O_CREAT | O_TRUNC);
    if (err) {
        debug_logs::recorderLogging("Can't open %s (%d).", path, err);
        return false;
    }
    segmentOpen = true;
    segmentWriteFailed = false;
    segmentBytes = 0;
    segmentStartMs = millis();
    return true;
}

/**
 * @brief Flush and close the open segment.
 *
 * @return The segment's final size in bytes.
 *
 */
uint32_t closeSegment() {
    if (!segmentOpen) return 0;
    flushMotion();
    segment.close();
    segmentOpen = false;
    return segmentBytes;
}

/** @brief Open the ring slot at @c ringHead (overwriting whatever it held). */
bool openRingSlot() {
    char path[kPathBytes];
    ringPath(ringHead, path, sizeof(path));
    return openSegment(path);
}

/**
 * @brief Write an event header file.
 *
 * @param header The header.
 *
 * @return Whether it was written.
 *
 */
bool writeMeta(const EventMeta& header) {
    char path[kPathBytes];
    metaPath(header.eventId, path, sizeof(path));
    mbed::File file;
    if (file.open(&fs, path, O_WRONLY | O_CREAT | O_TRUNC)) return false;
    bool ok = file.write(&header, sizeof(header)) == static_cast<ssize_t>(sizeof(header));
    file.close();
    return ok;
}

/**
 * @brief Read an event header file.
 *
 * @param id Event id.
 * @param header Receives the header.
 *
 * @return Whether a valid header was read.
 *
 */
bool readMeta(uint16_t id, EventMeta& header) {
    char path[kPathBytes];
    metaPath(id, path, sizeof(path));
    mbed::File file;
    if (file.open(&fs, path, O_RDONLY)) return false;
    bool ok = file.read(&header, sizeof(header)) == static_cast<ssize_t>(sizeof(header));
    file.close();
    return ok && memcmp(header.magic, "NVEV", 4) == 0;
}

/**
 * @brief Size of a file, or -1 if it doesn't exist.
 *
 * @param path Path within the filesystem.
 *
 * @return Size in bytes, or -1.
 *
 */
long fileSize(const char* path) {
    struct stat info;
    if (fs.stat(path, &info)) return -1;
    return static_cast<long>(info.st_size);
}

/**
 * @brief Collect the stored event ids, sorted oldest (lowest) first.
 *
 * @param ids Destination.
 * @param maxIds Capacity of @p ids.
 *
 * @return Number of ids found (may exceed @p maxIds, only the first @p maxIds are stored).
 *
 */
uint16_t scanEventIds(uint16_t* ids, uint16_t maxIds) {
    // The entry is ~260 bytes: too big for the 3 KB main stack, too rarely needed to keep in RAM.
    struct dirent* entry = new (std::nothrow) struct dirent;
    if (entry == nullptr) return 0;
    mbed::Dir dir;
    if (dir.open(&fs, "ev")) {
        delete entry;
        return 0;
    }
    uint16_t found = 0;
    while (dir.read(entry) > 0) {
        char* end = nullptr;
        unsigned long id = strtoul(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0' || id == 0 || id > 65535) continue;
        if (found < maxIds) {
            // insertion sort, the list is short
            uint16_t i = found;
            while (i > 0 && ids[i - 1] > id) {
                ids[i] = ids[i - 1];
                --i;
            }
            ids[i] = static_cast<uint16_t>(id);
        }
        found++;
    }
    dir.close();
    delete entry;
    return found;
}

/**
 * @brief Remove every file of an event, then its directory.
 *
 * @param id Event id.
 *
 * @return Whether the directory was removed.
 *
 */
bool removeEventFiles(uint16_t id) {
    char path[kPathBytes];
    for (uint16_t i = 0; i < kMaxSegments; ++i) {
        segmentPath(id, i, path, sizeof(path));
        if (fs.remove(path) && i > 0) {
            // gap: check one more in case a segment was lost, then stop
            segmentPath(id, i + 1, path, sizeof(path));
            if (fileSize(path) < 0) break;
        }
    }
    metaPath(id, path, sizeof(path));
    fs.remove(path);
    eventDirPath(id, path, sizeof(path));
    return fs.remove(path) == 0;
}

/**
 * @brief Whether an event is waiting for (or in the middle of) background deletion.
 *
 * @param id Event id.
 *
 * @return Whether it's queued.
 *
 */
bool isPendingDelete(uint16_t id) {
    for (uint8_t i = 0; i < pendingDeleteCount; ++i) {
        if (pendingDeletes[i] == id) return true;
    }
    return false;
}

/**
 * @brief Delete an event: remove its header now, and its other files in the background.
 *
 * @details
 * Without its header the event no longer lists or reads, so it's gone as far
 * as clients are concerned. A power loss before the rest is removed leaves a
 * header-less directory, which startRecorder() queues again. If the queue is
 * full the files are removed right away instead.
 *
 * @param id Event id.
 *
 * @return Whether it was deleted or queued.
 *
 */
bool queueEventDelete(uint16_t id) {
    if (isPendingDelete(id)) return true;
    EventMeta header;
    uint32_t bytes = readMeta(id, header) ? kMetaBytes + header.dataBytes : 0;
    char path[kPathBytes];
    metaPath(id, path, sizeof(path));
    fs.remove(path);
    if (pendingDeleteCount == kMaxPendingDeletes) return removeEventFiles(id);
    pendingDeleteBytes[pendingDeleteCount] = bytes;
    pendingDeletes[pendingDeleteCount++] = id;
    return true;
}

/**
 * @brief Remove one file of the oldest event queued for deletion, or its directory once it's empty.
 *
 * @par Returns
 * Nothing.
 *
 */
void stepPendingDelete() {
    if (pendingDeleteCount == 0) return;
    uint16_t id = pendingDeletes[0];
    char path[kPathBytes];

    if (pendingDeleteSegment < kMaxSegments) {
        segmentPath(id, pendingDeleteSegment, path, sizeof(path));
        bool removed = fs.remove(path) == 0;
        pendingDeleteSegment++;
        if (removed) return;
        // Missing: either the end, or a gap (a lost segment). Keep going only if the next one exists.
        segmentPath(id, pendingDeleteSegment, path, sizeof(path));
        if (fileSize(path) >= 0) return;
    }

    eventDirPath(id, path, sizeof(path));
    fs.remove(path);
    for (uint8_t i = 1; i < pendingDeleteCount; ++i) {
        pendingDeletes[i - 1] = pendingDeletes[i];
        pendingDeleteBytes[i - 1] = pendingDeleteBytes[i];
    }
    pendingDeleteCount--;
    pendingDeleteSegment = 0;
}

/**
 * @brief Finish an event left incomplete by a power loss: count its segments and mark it complete.
 *
 * @param id Event id.
 *
 * @par Returns
 * Nothing.
 *
 */
void repairEvent(uint16_t id) {
    EventMeta header;
    if (!readMeta(id, header) || header.complete) return;

    char path[kPathBytes];
    uint32_t bytes = 0;
    uint16_t count = 0;
    for (; count < kMaxSegments; ++count) {
        segmentPath(id, count, path, sizeof(path));
        long size = fileSize(path);
        if (size < 0) break;
        bytes += static_cast<uint32_t>(size);
    }
    header.segmentCount = count;
    header.dataBytes = bytes;
    header.complete = 1;
    header.endTimeMs = 0; // unknown, power was lost
    writeMeta(header);
    debug_logs::recorderLogging("Repaired event %u (%u segments).", id, count);
}

/**
 * @brief Free space on the flash, counting events still being deleted as free.
 *
 * @details
 * LittleFS works this out by walking every file's blocks, so it takes a
 * moment; it's only called after an event and at startup.
 *
 * @return Free bytes, or -1 if the filesystem can't tell.
 *
 */
int64_t freeFlashBytes() {
    struct statvfs info;
    if (fs.statvfs("", &info)) return -1;
    int64_t bytes = static_cast<int64_t>(info.f_bfree) * info.f_bsize;
    for (uint8_t i = 0; i < pendingDeleteCount; ++i) bytes += pendingDeleteBytes[i];
    return bytes;
}

/**
 * @brief Delete the oldest events until at most @c kMaxEvents remain and @c kReservedFlashBytes is free.
 *
 * @details
 * Never deletes the event being recorded. Deleting the event being
 * transferred ends that transfer.
 *
 * @par Returns
 * Nothing.
 *
 */
void enforceStorageLimits() {
    uint16_t ids[kMaxEvents + 2 + kMaxPendingDeletes];
    uint16_t capacity = sizeof(ids) / sizeof(ids[0]);
    uint16_t found = scanEventIds(ids, capacity);
    uint16_t stored = found < capacity ? found : capacity;
    // Events already being deleted still have directories, but don't count.
    uint16_t live = found;
    for (uint16_t i = 0; i < stored; ++i) {
        if (isPendingDelete(ids[i])) live--;
    }
    uint16_t excess = live > kMaxEvents ? live - kMaxEvents : 0;
    for (uint16_t i = 0; excess > 0 && i < stored; ++i) {
        if (isPendingDelete(ids[i])) continue;
        if (capturing && ids[i] == meta.eventId) continue;
        if (reading && ids[i] == readId) endEventRead();
        queueEventDelete(ids[i]);
        excess--;
        debug_logs::recorderLogging("Too many events, deleted oldest event %u.", ids[i]);
    }

    int64_t freeBytes = freeFlashBytes();
    for (uint16_t i = 0; freeBytes >= 0 && freeBytes < static_cast<int64_t>(kReservedFlashBytes) && i < stored; ++i) {
        if (isPendingDelete(ids[i])) continue;
        if (capturing && ids[i] == meta.eventId) continue;
        if (reading && ids[i] == readId) endEventRead();
        EventMeta header;
        uint32_t bytes = readMeta(ids[i], header) ? kMetaBytes + header.dataBytes : 0;
        queueEventDelete(ids[i]);
        freeBytes += bytes;
        debug_logs::recorderLogging("Low on space, deleted oldest event %u (%lu KB).", ids[i],
                                    static_cast<unsigned long>(bytes / 1024));
    }
}

/**
 * @brief Start a new event: move the ring into it and keep recording there.
 *
 * @return The new event's id, or 0 on failure (recording falls back to the ring).
 *
 */
uint16_t startEvent(TriggerType trigger, uint8_t detail, float magnitude, const char* label, uint32_t now) {
    uint16_t id = nextEventId;
    nextEventId = nextEventId == 65535 ? 1 : nextEventId + 1;

    // Close the segment being written: it's the newest part of the pre-trigger window.
    ringBytes[ringHead] = closeSegment();
    uint8_t ringFiles = ringCount + 1;

    char from[kPathBytes];
    char to[kPathBytes];
    eventDirPath(id, to, sizeof(to));
    int err = fs.mkdir(to, 0777);
    if (err && err != -EEXIST) {
        debug_logs::recorderLogging("Can't create event %u.", id);
        ringCount = 0;
        openRingSlot();
        return 0;
    }

    // Move the ring's files, oldest first, into the event as its first segments.
    eventDataBytes = 0;
    segmentCount = 0;
    for (uint8_t i = 0; i < ringFiles; ++i) {
        uint8_t slot = static_cast<uint8_t>((ringHead + kRingSlots - (ringFiles - 1) + i) % kRingSlots);
        ringPath(slot, from, sizeof(from));
        segmentPath(id, segmentCount, to, sizeof(to));
        if (fs.rename(from, to) == 0) {
            eventDataBytes += ringBytes[slot];
            segmentCount++;
        }
    }
    ringHead = 0;
    ringCount = 0;

    memset(&meta, 0, sizeof(meta));
    memcpy(meta.magic, "NVEV", 4);
    meta.formatVersion = kEventFormatVersion;
    meta.triggerType = static_cast<uint8_t>(trigger);
    meta.triggerDetail = detail;
    meta.eventId = id;
    meta.triggerTimeMs = now;
    meta.lastTriggerTimeMs = now;
    meta.triggerMagnitude = magnitude;
    meta.audioRateHz = kAudioRateHz;
    meta.motionRateHz = static_cast<uint16_t>(1000 / imu_config::kThreadRefreshIntervalMs);
    meta.accelRangeG = imu_config::kAccelRangeG;
    meta.gyroRangeDps = imu_config::kGyroRangeDps;
    meta.triggerCount = 1;
    strncpy(meta.label, label != nullptr ? label : "", sizeof(meta.label) - 1);
    writeMeta(meta); // incomplete header now, so a power loss leaves a repairable event

    segmentPath(id, segmentCount, to, sizeof(to));
    if (!openSegment(to)) {
        // Can't continue the event: finish it with what was moved.
        meta.segmentCount = segmentCount;
        meta.dataBytes = eventDataBytes;
        meta.complete = 1;
        meta.endTimeMs = now;
        writeMeta(meta);
        openRingSlot();
        return 0;
    }
    segmentCount++;

    capturing = true;
    eventStartMs = now;
    captureEndMs = now + kPostTriggerMs;
    debug_logs::recorderLogging("Event %u started (%s), %u pre-trigger segments.", id, meta.label, segmentCount - 1);
    return id;
}

/**
 * @brief Finish the event: close its last segment, write its final header, resume the ring.
 *
 * @par Returns
 * Nothing.
 *
 */
void finishEvent(uint32_t now) {
    eventDataBytes += closeSegment();
    meta.segmentCount = segmentCount;
    meta.dataBytes = eventDataBytes;
    meta.endTimeMs = now;
    meta.complete = 1;
    bool saved = writeMeta(meta);
    capturing = false;

    debug_logs::recorderLogging("Event %u finished: %u segments, %lu bytes%s.", meta.eventId, segmentCount,
        static_cast<unsigned long>(eventDataBytes), saved ? "" : " (header write FAILED)");
    if (readyHandler != nullptr) readyHandler(meta.eventId, kMetaBytes + eventDataBytes, static_cast<TriggerType>(meta.triggerType));

    enforceStorageLimits();
    ringHead = 0;
    ringCount = 0;
    openRingSlot();
}

/**
 * @brief Close the current segment and open the next one (in the event or the ring).
 *
 * @par Returns
 * Nothing.
 *
 */
void rotateSegment() {
    if (capturing) {
        if (segmentCount >= kMaxSegments) {
            finishEvent(millis());
            return;
        }
        eventDataBytes += closeSegment();
        char path[kPathBytes];
        segmentPath(meta.eventId, segmentCount, path, sizeof(path));
        if (openSegment(path)) segmentCount++;
        return;
    }

    ringBytes[ringHead] = closeSegment();
    if (ringCount < kRingSlots - 1) ringCount++;
    ringHead = static_cast<uint8_t>((ringHead + 1) % kRingSlots);
    openRingSlot();
}

/**
 * @brief Open segment @p index of the event being read.
 *
 * @param index Segment index.
 * @param start Stream offset of the segment's first byte.
 *
 * @return Whether the file is open.
 *
 */
bool openReadSegment(int index, uint32_t start) {
    if (readFileIndex >= 0) readFile.close();
    readFileIndex = -1;
    char path[kPathBytes];
    segmentPath(readId, static_cast<uint16_t>(index), path, sizeof(path));
    if (readFile.open(&fs, path, O_RDONLY)) return false;
    off_t size = readFile.size();
    readFileIndex = index;
    readFileStart = start;
    readFileBytes = size > 0 ? static_cast<uint32_t>(size) : 0;
    return true;
}

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the recorder, declared in recorder.h.
 * @{
 */
bool startRecorder() {
    int err = fs.mount(&flash);
    if (err) {
        // Never reformat here: the NDP firmware files live on this filesystem.
        debug_logs::recorderLogging("Can't mount the external flash (%d).", err);
        return false;
    }

    fs.mkdir("ring", 0777);
    fs.mkdir("ev", 0777);

    // The rolling buffer starts empty every boot.
    char path[kPathBytes];
    for (uint8_t slot = 0; slot < kRingSlots; ++slot) {
        ringPath(slot, path, sizeof(path));
        fs.remove(path);
    }

    // Find the newest event id, and finish any event a power loss interrupted.
    uint16_t ids[kMaxEvents + 2];
    uint16_t found = scanEventIds(ids, kMaxEvents + 2);
    uint16_t stored = found < kMaxEvents + 2 ? found : kMaxEvents + 2;
    for (uint16_t i = 0; i < stored; ++i) {
        // No header: a delete was cut short by a power loss. Finish it.
        metaPath(ids[i], path, sizeof(path));
        if (fileSize(path) < 0) {
            queueEventDelete(ids[i]);
            continue;
        }
        repairEvent(ids[i]);
    }
    if (stored > 0) nextEventId = ids[stored - 1] == 65535 ? 1 : ids[stored - 1] + 1;
    enforceStorageLimits();

    if (!openRingSlot()) return false;
    running = true;
    debug_logs::recorderLogging("Started recorder: %u stored events, %u ring slots of %lu ms, %ld KB free.",
        stored, kRingSlots, kSegmentMs, static_cast<long>(freeFlashBytes() / 1024));
    return true;
}

void updateRecorder() {
    if (!running) return;
    uint32_t now = millis();

    if (capturing && static_cast<int32_t>(now - captureEndMs) >= 0) {
        finishEvent(now);
        return;
    }
    if (segmentOpen && now - segmentStartMs >= kSegmentMs) {
        rotateSegment();
        return; // one slow flash operation per call
    }
    stepPendingDelete();
}

void recordAudioChunk(const int16_t* samples, size_t count, uint8_t missedChunks, uint32_t timestampMs) {
    if (!segmentOpen) return;

    // Timestamp the record at its first sample, not at extraction.
    uint32_t startMs = timestampMs - static_cast<uint32_t>(count * 1000UL / kAudioRateHz);
    while (count > 0) {
        size_t n = count < kMaxAudioSamplesPerRecord ? count : kMaxAudioSamplesPerRecord;
        AudioRecordHeader audio = {adpcmState.predictor, adpcmState.index, 0, static_cast<uint16_t>(n)};
        size_t codeBytes = (n + 1) / 2;
        RecordHeader header = {static_cast<uint8_t>(RecordType::Audio), missedChunks,
                               static_cast<uint16_t>(sizeof(audio) + codeBytes), startMs};
        writeSegment(&header, sizeof(header));
        writeSegment(&audio, sizeof(audio));

        // Encode and write 128 samples (64 bytes) at a time, so no chunk-sized buffer is needed.
        uint8_t codes[64];
        for (size_t done = 0; done < n; done += 2 * sizeof(codes)) {
            size_t piece = n - done < 2 * sizeof(codes) ? n - done : 2 * sizeof(codes);
            adpcm::encode(adpcmState, samples + done, piece, codes);
            writeSegment(codes, (piece + 1) / 2);
        }

        samples += n;
        count -= n;
        startMs += static_cast<uint32_t>(n * 1000UL / kAudioRateHz);
        missedChunks = 0;
    }
}

void recordMotionSample(const ImuSample& sample) {
    if (!segmentOpen) return;
    if (motionCount == 0) motionStartMs = sample.timestampMs;

    uint32_t offset = sample.timestampMs - motionStartMs;
    MotionSample& out = motionBuffer[motionCount++];
    out.offsetMs = static_cast<uint16_t>(offset > 65535 ? 65535 : offset);
    memcpy(out.accel, sample.accel, sizeof(out.accel));
    memcpy(out.gyro, sample.gyro, sizeof(out.gyro));

    if (motionCount >= kMotionSamplesPerRecord) flushMotion();
}

uint16_t triggerEvent(TriggerType trigger, uint8_t detail, float magnitude, const char* label) {
    if (!running) return 0;
    uint32_t now = millis();

    if (capturing) {
        // Extend: keep recording kPostTriggerMs past this trigger, within the event's maximum length.
        uint32_t end = now + kPostTriggerMs;
        uint32_t latest = eventStartMs + kMaxEventMs;
        if (static_cast<int32_t>(end - latest) > 0) end = latest;
        if (static_cast<int32_t>(end - captureEndMs) > 0) captureEndMs = end;
        meta.lastTriggerTimeMs = now;
        meta.triggerCount++;
        MarkRecord mark = {detail, {0, 0, 0}, magnitude};
        appendRecord(RecordType::Mark, static_cast<uint8_t>(trigger), now, &mark, sizeof(mark));
        debug_logs::recorderLogging("Event %u extended by trigger %u (%s).", meta.eventId, meta.triggerCount, label);
        return meta.eventId;
    }

    uint16_t id = startEvent(trigger, detail, magnitude, label, now);
    if (id == 0) return 0;

    MarkRecord mark = {detail, {0, 0, 0}, magnitude};
    appendRecord(RecordType::Mark, static_cast<uint8_t>(trigger), now, &mark, sizeof(mark));
    if (startedHandler != nullptr) startedHandler(id, trigger, detail, now);
    return id;
}

void setEventHandlers(EventStartedHandler started, EventReadyHandler ready) {
    startedHandler = started;
    readyHandler = ready;
}

uint8_t listEvents(EventInfo* out, uint8_t maxEvents) {
    if (!running) return 0;
    uint16_t ids[kMaxEvents + 2];
    uint16_t found = scanEventIds(ids, kMaxEvents + 2);
    uint16_t stored = found < kMaxEvents + 2 ? found : kMaxEvents + 2;

    uint8_t count = 0;
    EventMeta header;
    for (uint16_t i = 0; i < stored && count < maxEvents; ++i) {
        if (!readMeta(ids[i], header)) continue;
        bool recording = capturing && ids[i] == meta.eventId;
        EventInfo& info = out[count++];
        info.id = ids[i];
        info.complete = header.complete && !recording;
        info.size = info.complete ? kMetaBytes + header.dataBytes : 0;
        info.trigger = static_cast<TriggerType>(header.triggerType);
    }
    return count;
}

bool deleteEvent(uint16_t id) {
    if (!running || (capturing && id == meta.eventId) || isPendingDelete(id)) return false;
    char path[kPathBytes];
    metaPath(id, path, sizeof(path));
    if (fileSize(path) < 0) return false; // no such event
    if (reading && id == readId) endEventRead();
    return queueEventDelete(id);
}

bool beginEventRead(uint16_t id, uint32_t& totalBytes) {
    endEventRead();
    if (!running || (capturing && id == meta.eventId)) return false;
    if (!readMeta(id, readHeader) || !readHeader.complete) return false;

    // The header's dataBytes is the segments' total size (written from the
    // actual file sizes when the event finished, or by repairEvent()).
    // Looking up each segment instead blocked for seconds on a long event
    // (one directory search per file), long enough to drop audio.
    totalBytes = kMetaBytes + readHeader.dataBytes;

    readId = id;
    reading = true;
    readFileIndex = -1;
    return true;
}

int readEvent(uint32_t offset, uint8_t* buffer, size_t maxBytes) {
    if (!reading) return -1;

    if (offset < kMetaBytes) {
        size_t n = kMetaBytes - offset;
        if (n > maxBytes) n = maxBytes;
        memcpy(buffer, reinterpret_cast<const uint8_t*>(&readHeader) + offset, n);
        return static_cast<int>(n);
    }

    // Reads go forward through the segments; a backwards jump (a resumed transfer) restarts from the first.
    if (readFileIndex < 0 || offset < readFileStart) {
        if (!openReadSegment(0, kMetaBytes)) return 0;
    }
    while (offset >= readFileStart + readFileBytes) {
        int next = readFileIndex + 1;
        if (next >= readHeader.segmentCount) return 0; // end of stream
        if (!openReadSegment(next, readFileStart + readFileBytes)) return -1;
    }

    uint32_t position = offset - readFileStart;
    if (readFile.seek(position, SEEK_SET) != static_cast<off_t>(position)) return -1;
    size_t n = readFileBytes - position;
    if (n > maxBytes) n = maxBytes;
    ssize_t got = readFile.read(buffer, n);
    return got < 0 ? -1 : static_cast<int>(got);
}

void endEventRead() {
    if (readFileIndex >= 0) readFile.close();
    readFileIndex = -1;
    reading = false;
}
/** @} */ // end of Public
