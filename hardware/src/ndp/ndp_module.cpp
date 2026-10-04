/**
 * @file ndp_module.cpp
 *
 * @brief Implementation of the Syntiant NDP120 startup and keyword module.
 *
 * @details
 * Wraps the core's @c NDP library. @c NDP.begin() discards its package load
 * result, so success is confirmed afterward by asking the audio pipeline for
 * its chunk size, which only answers once all three packages are running. The
 * library's error callback is used purely to count failures, it never blocks.
 *
 * Keyword matches are read without the library's interrupt path. That path
 * runs on a higher-priority thread that talks to the NDP over the SPI bus the
 * external flash also uses, so it could interleave with a flash write. And the
 * library's @c NDP.poll() delays 100 ms whenever there is no match. Instead,
 * the NDP's own driver counts every match it sees on the device struct (from
 * any poll, including the mailbox waits inside audio extraction), and
 * @c pollKeywordMatch() reads the match result whenever that count goes up.
 * (The driver's match ring isn't used: on this firmware its producer index
 * isn't a ring position, so the ring always looks non-empty.)
 * The driver's device pointer is private to @c NDPClass, so it is reached
 * with the standard explicit-instantiation access idiom below.
 *
 */

#include <Arduino.h>
#include <NDP.h>

#include "ndp_module.h"
#include "../configs.h"
#include "../logger.h"
#include "../module_utils.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the NDP module.
 * These are not intended to be used outside of this module.
 * @{
 */
/**
 * @brief Tag naming the private @c NDPClass::iif member for @c PrivateAccess (see module_utils.h).
 *
 * @details
 * This reads one field the library sets in @c NDP.begin() (@c iif.d, the
 * driver device) and changes nothing.
 *
 */
struct NdpIifTag {
    typedef syntiant_ndp120_tiny_integration_interfaces_s NDPClass::*type;
    friend type get(NdpIifTag);
};

namespace {
/** @brief Whether @c startNDPModule() succeeded. */
bool ready = false;
/** @brief Number of times the NDP library reported an error. */
volatile uint16_t errorCount = 0;
/** @brief The NDP driver's device, reached through @c NDPClass's private integration interface. */
syntiant_ndp120_tiny_device_s* device = nullptr;
/** @brief Whether @c startKeywordDetection() succeeded. */
bool keywordsOn = false;
/** @brief Keyword matches handed to the caller. */
uint32_t matchesTaken = 0;
/** @brief The driver's match count when last checked, see @c pollKeywordMatch(). */
uint32_t matchesSeen = 0;
/** @brief millis() of the last keyword statistics log. */
unsigned long lastStatsLogMs = 0;

} // namespace
template struct PrivateAccess<NdpIifTag, &NDPClass::iif>;
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
    debug_logs::flushLogs(); // the load blocks for seconds; show what it's doing first
    NDP.begin(ndp_config::kMcuFirmware);
    bool loaded = errorCount == 0 &&
                  NDP.load(ndp_config::kDspFirmware) && errorCount == 0 &&
                  NDP.load(ndp_config::kModel) && errorCount == 0;

    if (!loaded || NDP.getAudioChunkSize() <= 0) {
        debug_logs::ndpLogging("Failed to load packages (%u errors). Are they on the board's flash? See README.md.", errorCount);
        return false;
    }

    device = static_cast<syntiant_ndp120_tiny_device_s*>((NDP.*get(NdpIifTag())).d);

    // Read the model's labels (NDP.getLabels()).
    NDP.getInfo();

    ready = true;
    debug_logs::ndpLogging("Started NDP module.");
    return true;
}

bool isNDPReady() {
    return ready;
}

bool startKeywordDetection() {
    if (!ready) return false;
    // As in Arduino's AlexaDemo, after the microphone is on. This only
    // configures the NDP; the library's MCU-side interrupt handler is never
    // attached, see the file comment.
    int interrupts = SYNTIANT_NDP120_INTERRUPT_DEFAULT;
    if (syntiant_ndp120_tiny_interrupts(device, &interrupts)) {
        debug_logs::ndpLogging("Failed to enable NDP match reporting.");
        return false;
    }
    matchesSeen = device->matches; // skip any match from before now
    keywordsOn = true;
    lastStatsLogMs = millis();

    char** labels = NDP.getLabels();
    debug_logs::ndpLogging("Listening for keywords (first label: %s).",
                           (labels != nullptr && labels[0] != nullptr) ? labels[0] : "none");
    return true;
}

int pollKeywordMatch(const char** label) {
    if (!keywordsOn) return -1;

#if NICLA_DEBUG
    // Matches the NDP reported (counted by its driver from any poll, including
    // those inside audio extraction) against matches handed out here. If the
    // first stays 0 while saying the keyword, the NDP isn't detecting it.
    if (millis() - lastStatsLogMs >= debug_config::kKeywordStatsDelay) {
        lastStatsLogMs = millis();
        debug_logs::ndpLogging("Keyword stats: NDP reported %lu, taken %lu.",
                               static_cast<unsigned long>(device->matches), static_cast<unsigned long>(matchesTaken));
    }
#endif

    // Process any pending mailbox messages (this is where the driver counts
    // matches), then read the match result if the count went up. Matches
    // closer together than one poll (50 ms) count as one.
    uint32_t notifications = 0;
    if (syntiant_ndp120_tiny_poll(device, &notifications, 1)) return -1;
    if (device->matches == matchesSeen) return -1;
    matchesSeen = device->matches;

    // get_match_summary() only reads the result when its ring looks non-empty,
    // so make sure it does.
    uint8_t network = device->last_network_id;
    device->match_consumer[network] = device->match_producer[network] + 1;
    uint32_t summary = 0;
    if (syntiant_ndp120_tiny_get_match_summary(device, &summary)) return -1;
    if (!(summary & NDP120_SPI_MATCH_MATCH_MASK)) return -1;

    int index = NDP120_SPI_MATCH_WINNER_EXTRACT(summary);
    matchesTaken++;
    if (label != nullptr) {
        char** labels = NDP.getLabels();
        *label = (labels != nullptr && labels[index] != nullptr) ? labels[index] : "?";
    }
    return index;
}
/** @} */ // end of Public
