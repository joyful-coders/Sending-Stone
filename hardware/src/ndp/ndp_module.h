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
 * board's external flash into the NDP120, in that order. Every other sensor
 * module depends on this: the BMI270 and BMM150 sit on the NDP120's SPI bus,
 * and the microphone feeds its audio pipeline. Must run after @c
 * nicla::begin() and before @c startIMUModule(), @c startMagModule(), and @c
 * startSoundModule().
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
