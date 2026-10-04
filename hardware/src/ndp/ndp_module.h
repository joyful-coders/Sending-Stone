/**
 * @headerfile ndp_module.h "src/ndp/ndp_module.h"
 *
 */

#pragma once

/**
 * @brief Start the Syntiant NDP120 by loading its firmware packages.
 *
 * @details
 * Loads @c ndp_config::kMcuFirmware, @c kDspFirmware, and @c kModel from the
 * board's external flash into the NDP120, in that order, reads the model's
 * keyword labels, and enables match reporting. Every sensor module depends on
 * this: the BMI270 sits on the NDP120's SPI bus, and the microphone feeds its
 * audio pipeline. Must run after @c nicla::begin() and before @c
 * startIMUModule() and @c startAudioModule(), and before the recorder mounts
 * the flash (the library unmounts it after loading).
 *
 * @note
 * Takes a few seconds, the packages are streamed over SPI.
 *
 * @par Parameters
 * None.
 *
 * @return The status of the NDP startup attempt.
 * @retval true Every package loaded and the audio pipeline reports a valid chunk size.
 * @retval false A package was missing from flash or failed to load.
 *
 */
bool startNDPModule();

/**
 * @brief Whether @c startNDPModule() succeeded.
 *
 * @par Parameters
 * None.
 *
 * @return Whether the NDP120 is loaded and usable.
 *
 */
bool isNDPReady();

/**
 * @brief Have the NDP start reporting keyword matches.
 *
 * @details
 * Call after the microphone is on (startAudioModule()), as Arduino's own
 * keyword example does. Any match from before is discarded.
 *
 * @return Whether it started.
 *
 */
bool startKeywordDetection();

/**
 * @brief Take the oldest keyword match the NDP has reported, if any, without blocking.
 *
 * @details
 * Must be called from the main loop (the same thread as every other NDP and
 * flash access). Matches are counted inside the NDP driver, so one that
 * arrives while audio is being extracted is still picked up on the next call.
 *
 * @param label If not null, receives the matched class's label from the model (e.g. "NN0:alexa").
 *
 * @return The matched class index, or -1 if there is no new match.
 *
 */
int pollKeywordMatch(const char** label);
