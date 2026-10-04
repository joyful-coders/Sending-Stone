/**
 * @file logger.h
 * @headerfile logger.h "src/logger.h"
 *
 * @brief Fixed-size, non-allocating queue of debug log messages.
 *
 * @details
 * Only exists in debug builds (@c NICLA_DEBUG = 1). In a normal build every
 * logger below is an empty stub and the queue isn't allocated at all.
 *
 */

#pragma once

#include "configs.h"
#include <stdarg.h>
#include <stdio.h>

namespace debug_logs {
#if NICLA_DEBUG
/** @brief Maximum number of log messages that can be queued. Kept small, the nRF52832 has 64 KB of RAM (setup() flushes between steps). */
constexpr uint8_t kMaxMessages = 8;
/** @brief Maximum size, in bytes, of each message body (the prefix is stored separately). */
constexpr uint8_t kMessageSize = 88;

/** @brief A single queued log message. */
struct LogMessage {
    /** @brief Module prefix, such as "[IMU]". Points at a string literal from configs.h, never copied. */
    const char* prefix;
    /** @brief Formatted, null terminated message body. */
    char text[kMessageSize];
};

/** @brief Ring buffer holding queued log messages, defined in logger.cpp. */
extern LogMessage queue[kMaxMessages];
/** @brief Index of the next slot @c pushLog() will write to, defined in logger.cpp. */
extern uint8_t head;
/** @brief Index of the next slot @c flushLogs() will print, defined in logger.cpp. */
extern uint8_t tail;
/** @brief Messages rejected because the queue was full since the last flush, defined in logger.cpp. */
extern uint16_t dropped;

/**
 * @brief Format a message straight into the next free queue slot.
 *
 * @details
 * The queue is a fixed-size ring buffer with no dynamic allocation, so a full
 * queue rejects the new message (counted in @c dropped) rather than growing.
 * Only the main loop logs, so no locking is needed.
 *
 * @param prefix Module prefix, such as "[IMU]". Must outlive the queue (a string literal).
 * @param fmt Printf style format string for the message body.
 * @param args Variable argument list matching @p fmt.
 *
 * @return Whether the message was queued.
 * @retval true The message was queued.
 * @retval false The queue was full and the message was dropped, or verbose logging is disabled.
 *
 */
inline bool pushLog(const char *prefix, const char *fmt, va_list args) {
    if (!debug_config::kEnableVerboseLogging) return false;
    uint8_t nextHead = (head + 1) % kMaxMessages;

    if (nextHead == tail) {
        dropped++;
        return false;
    }

    queue[head].prefix = prefix;
    vsnprintf(queue[head].text, kMessageSize, fmt, args);
    head = nextHead;
    return true;
}

/**
 * @brief Print every queued log message to Serial, then clear the queue.
 *
 * @details
 * Prints @p separator, an uptime header (plus a dropped count, if any), then
 * every queued message in order, straight from the queue. Intended to be
 * called periodically from the main loop rather than from inside @c
 * pushLog(), so Serial output stays batched. Skipped entirely when verbose
 * logging is disabled, since Serial itself is never started in that case.
 *
 * @param separator Line printed before the uptime header, once per flush.
 *
 * @return Whether any messages were printed.
 * @retval true One or more messages were printed.
 * @retval false The queue was empty or verbose logging is disabled.
 *
 */
inline bool flushLogs(const char *separator = "---------------------------------------------------") {
    if (!debug_config::kEnableVerboseLogging) return false;
    if (head == tail) return false; // No messages to flush

    Serial.println(separator);
    unsigned long seconds = millis() / 1000;
    char header[64];
    snprintf(header, sizeof(header), "Hours: %lu Minutes: %lu Seconds: %lu", (seconds / 3600) % 24, (seconds / 60) % 60, seconds % 60);
    Serial.print(header);
    if (dropped > 0) {
        Serial.print(" (");
        Serial.print(dropped);
        Serial.print(" dropped)");
        dropped = 0;
    }
    Serial.println();

    for (; tail != head; tail = (tail + 1) % kMaxMessages) {
        Serial.print(queue[tail].prefix);
        Serial.print(' ');
        Serial.println(queue[tail].text);
    }
    return true;
}

#else // normal build: no queue, every call compiles to nothing

/** @brief Stub, logging is compiled out of normal builds. */
inline bool pushLog(const char*, const char*, va_list) { return false; }
/** @brief Stub, logging is compiled out of normal builds. */
inline bool flushLogs(const char* = nullptr) { return false; }

#endif // NICLA_DEBUG

/**
 * @brief Define a named logger function gated by an enabled flag.
 *
 * @details
 * Expands to an inline function @c name(fmt, ...) that forwards to @c
 * pushLog() with @p prefix, but only when @p enabled is true at compile time.
 * Used below to generate one differently prefixed logger per module without
 * repeating the va_list boilerplate for each one.
 *
 * @param name Identifier for the generated logger function.
 * @param enabled Compile time boolean gating whether this logger emits.
 * @param prefix Text prepended to every message from this logger.
 *
 * @return Whether the message was queued.
 * @retval true The message was queued.
 * @retval false enabled was false, the queue was full, or verbose logging is disabled overall.
 *
 */
#define DEFINE_LOGGER(name, enabled, prefix)        \
    inline bool name(const char *fmt, ...) {        \
        if (!(enabled)) return false;               \
        va_list args;                               \
        va_start(args, fmt);                        \
        bool result = pushLog(prefix, fmt, args);   \
        va_end(args);                               \
        return result;                              \
    }

    DEFINE_LOGGER(mainLogging,     debug_config::kEnableMainLogging,     debug_config::kMainPrefix)
    DEFINE_LOGGER(ledLogging,      debug_config::kEnableLEDLogging,      debug_config::kLEDPrefix)
    DEFINE_LOGGER(ndpLogging,      debug_config::kEnableNDPLogging,      debug_config::kNDPPrefix)
    DEFINE_LOGGER(imuLogging,      debug_config::kEnableIMULogging,      debug_config::kIMUPrefix)
    DEFINE_LOGGER(audioLogging,    debug_config::kEnableAudioLogging,    debug_config::kAudioPrefix)
    DEFINE_LOGGER(recorderLogging, debug_config::kEnableRecorderLogging, debug_config::kRecorderPrefix)
    DEFINE_LOGGER(triggerLogging,  debug_config::kEnableTriggerLogging,  debug_config::kTriggerPrefix)
    DEFINE_LOGGER(bleLogging,      debug_config::kEnableBLELogging,      debug_config::kBLEPrefix)
#undef DEFINE_LOGGER
} // namespace debug_logs
