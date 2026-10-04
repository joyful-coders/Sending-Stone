/**
 * @file module_utils.h
 * @headerfile module_utils.h "src/module_utils.h"
 *
 * @brief Small helpers shared by every module: cooperative thread scheduling
 * and storage for a module's latest reading.
 *
 */

#pragma once

// ArduinoThread's Thread class. Included through ThreadController.h because on
// the mbed core <Thread.h> resolves to mbed's own rtos/Thread.h instead.
#include <ThreadController.h>

/**
 * @brief Create a cooperative thread that stays idle until it is enabled.
 *
 * @details
 * Modules create their threads at static-init time with their final interval,
 * then set @c Thread::enabled once their hardware has started. Until then (or
 * forever, if starting failed) @c runIfDue() is a no-op, so no separate
 * "started" flag is needed.
 *
 * @param callback Function run on each tick.
 * @param intervalMs Interval, in milliseconds, between ticks.
 *
 * @return The disabled thread.
 *
 */
inline Thread makeIdleThread(void (*callback)(), unsigned long intervalMs) {
    Thread thread(callback, intervalMs);
    thread.enabled = false;
    return thread;
}

/**
 * @brief Run @p thread's callback if it is enabled and its interval has elapsed.
 *
 * @param thread Thread to tick.
 *
 * @par Returns
 * Nothing.
 *
 */
inline void runIfDue(Thread& thread) {
    if (thread.shouldRun()) thread.run();
}

/**
 * @brief The newest reading a module has taken, and whether there is one yet.
 *
 * @tparam Reading The module's reading struct.
 *
 */
template <typename Reading>
class LatestReading {
public:
    /**
     * @brief Replace the stored reading and mark it valid.
     *
     * @param reading The new reading.
     *
     */
    void store(const Reading& reading) {
        value_ = reading;
        valid_ = true;
    }

    /**
     * @brief Copy out the stored reading, if there is one.
     *
     * @param out Destination, left untouched when there is no reading yet.
     *
     * @return Whether a reading was copied.
     *
     */
    bool copyTo(Reading& out) const {
        if (valid_) out = value_;
        return valid_;
    }

    /** @brief Whether a reading has been stored. */
    bool valid() const { return valid_; }

    /** @brief The stored reading, only meaningful when @c valid(). */
    const Reading& value() const { return value_; }

private:
    Reading value_{};
    bool valid_ = false;
};
