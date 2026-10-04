/**
 * @file ndp_module.cpp
 *
 * @brief Implementation of the Syntiant NDP120 startup module.
 *
 * @details
 * Wraps the core's @c NDP library. @c NDP.begin() discards its package load
 * result, so success is confirmed afterward by asking the audio pipeline for
 * its chunk size, which only answers once all three packages are running. The
 * library's error callback is used purely to count failures, it never blocks.
 *
 */

#include <Arduino.h>
#include <NDP.h>

#include "ndp_module.h"
#include "../configs.h"
#include "../logger.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the NDP module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief Whether @c startNDPModule() succeeded. */
bool ready = false;
/** @brief Number of times the NDP library reported an error. */
volatile uint16_t errorCount = 0;

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the NDP module, declared in ndp_module.h.
 * @{
 */
bool startNDPModule() {
    NDP.onError([]() { errorCount++; });

    debug_logs::ndpLogging("Loading %s, %s, %s...", ndp_config::kMcuFirmware, ndp_config::kDspFirmware, ndp_config::kModel);
    // NDP.begin() swallows its own load result, so a missing MCU firmware only
    // shows up as an error callback. Stop there: loading further packages into
    // an NDP with no MCU firmware can hang inside the library.
    NDP.begin(ndp_config::kMcuFirmware);
    bool loaded = errorCount == 0 &&
                  NDP.load(ndp_config::kDspFirmware) && errorCount == 0 &&
                  NDP.load(ndp_config::kModel) && errorCount == 0;

    if (!loaded || NDP.getAudioChunkSize() <= 0) {
        debug_logs::ndpLogging("Failed to load packages (%u errors). Are they on the board's flash? See README.md.", errorCount);
        return false;
    }

    ready = true;
    debug_logs::ndpLogging("Started NDP module.");
    return true;
}

bool isNDPReady() {
    return ready;
}
/** @} */ // end of Public
