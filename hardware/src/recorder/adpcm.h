/**
 * @file adpcm.h
 * @headerfile adpcm.h "src/recorder/adpcm.h"
 *
 * @brief IMA-ADPCM encoder: 16-bit PCM to 4-bit codes, 4:1 compression.
 *
 * @details
 * The standard IMA/DVI ADPCM algorithm, so any IMA decoder can play it back
 * given the starting predictor and step index (stored in every audio record,
 * see event_format.h). Cheap enough to run on every sample in real time.
 *
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace adpcm {
/** @brief Step index change for each 4-bit code (sign bit ignored). */
constexpr int8_t kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

/** @brief Quantizer step size for each step index. */
constexpr int16_t kStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767};

/** @brief Encoder state carried from one sample (and one chunk) to the next. */
struct State {
    /** @brief Current predicted sample value. */
    int16_t predictor = 0;
    /** @brief Current step index, 0-88. */
    uint8_t index = 0;
};

/**
 * @brief Encode one sample, updating @p state exactly as a decoder will.
 *
 * @param state Encoder state.
 * @param sample 16-bit PCM sample.
 *
 * @return The 4-bit code.
 *
 */
inline uint8_t encodeSample(State& state, int16_t sample) {
    int step = kStepTable[state.index];
    int diff = sample - state.predictor;
    uint8_t code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }

    int delta = step >> 3;
    if (diff >= step) { code |= 4; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { code |= 2; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { code |= 1; delta += step; }

    int predicted = state.predictor + ((code & 8) ? -delta : delta);
    if (predicted > 32767) predicted = 32767;
    if (predicted < -32768) predicted = -32768;
    state.predictor = static_cast<int16_t>(predicted);

    int index = state.index + kIndexTable[code];
    state.index = static_cast<uint8_t>(index < 0 ? 0 : (index > 88 ? 88 : index));
    return code;
}

/**
 * @brief Encode a block of samples, two per output byte, low nibble first.
 *
 * @param state Encoder state, updated.
 * @param samples Input samples.
 * @param count Number of samples.
 * @param out Output, at least @c (count + 1) / 2 bytes.
 *
 * @par Returns
 * Nothing.
 *
 */
inline void encode(State& state, const int16_t* samples, size_t count, uint8_t* out) {
    for (size_t i = 0; i < count; i += 2) {
        uint8_t low = encodeSample(state, samples[i]);
        uint8_t high = (i + 1 < count) ? encodeSample(state, samples[i + 1]) : 0;
        out[i / 2] = static_cast<uint8_t>(low | (high << 4));
    }
}

} // namespace adpcm
