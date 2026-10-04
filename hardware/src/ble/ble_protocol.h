/**
 * @file ble_protocol.h
 * @headerfile ble_protocol.h "src/ble/ble_protocol.h"
 *
 * @brief Wire format of the sensor packet streamed over BLE.
 *
 * @details
 * This is the contract shared by @c ble.cpp and any receiving client. Every
 * notification on the readings characteristic carries exactly one @c
 * SensorPacket: 30 bytes, packed, little-endian, with every value sent as a
 * whole number. Divide each scaled field by its @c k*Scale constant below to
 * recover the real-world value. See BLE_PROTOCOL.md for the client-side
 * decoding guide.
 *
 * Fields are append-only: everything after byte 20 was added later, so a
 * client that never negotiates a larger ATT MTU (and so only receives the
 * first 20 bytes) still decodes the original fields correctly.
 *
 * @note
 * Any change to existing offsets must bump @c BLE_PACKET_VERSION and update
 * BLE_PROTOCOL.md, since clients decode the bytes positionally.
 *
 */

#pragma once

#include <Arduino.h>

/** @brief Current sensor packet format version, readable from the version characteristic. */
constexpr uint8_t BLE_PACKET_VERSION = 1;

namespace ble_protocol {
/** @brief Acceleration fields are m/s^2 multiplied by this (0.01 m/s^2 resolution, +-327.67 m/s^2 span). */
constexpr float kAccelScale = 100.0f;
/** @brief Gyro fields are rad/s multiplied by this (0.001 rad/s resolution, +-32.767 rad/s span). */
constexpr float kGyroScale = 1000.0f;
/** @brief Temperature field is degrees Celsius multiplied by this (0.01 C resolution). */
constexpr float kTempScale = 100.0f;
/** @brief Magnetometer fields are microtesla multiplied by this (0.1 uT resolution, +-3276.7 uT span). */
constexpr float kMagScale = 10.0f;

/** @brief @c SensorPacket::flags bit set while the board runs from its battery. */
constexpr uint8_t kFlagOnBattery = 1 << 0;
/** @brief @c SensorPacket::flags bit set while the battery is charging. */
constexpr uint8_t kFlagCharging = 1 << 1;

/**
 * @brief Scale a real-world value into a saturating @c int16_t field.
 *
 * @details
 * Rounds to the nearest whole number, then clamps to the @c int16_t range so
 * an out-of-range reading saturates at the limit instead of wrapping around
 * to the opposite sign.
 *
 * @param value Real-world value to encode.
 * @param scale Multiplier applied before rounding, one of the @c k*Scale constants.
 *
 * @return The scaled, rounded, clamped value.
 *
 */
inline int16_t encodeScaled(float value, float scale) {
    float scaled = roundf(value * scale);
    if (scaled > INT16_MAX) return INT16_MAX;
    if (scaled < INT16_MIN) return INT16_MIN;
    return static_cast<int16_t>(scaled);
}

} // namespace ble_protocol

/**
 * @brief One combined sample of every onboard sensor, sent once per notification.
 *
 * @details
 * Packed with no padding and stored little-endian (the nRF52832's native byte
 * order). Byte offsets are fixed, see the table in BLE_PROTOCOL.md.
 *
 */
struct __attribute__((packed)) SensorPacket
{
    /** @brief [0] Device uptime, in milliseconds, when the packet was built. Wraps after ~49.7 days. */
    uint32_t timestampMs;
    /** @brief [4] BMI270 acceleration along X, Y, Z, scaled by @c ble_protocol::kAccelScale. */
    int16_t accel[3];
    /** @brief [10] BMI270 angular rate around X, Y, Z, scaled by @c ble_protocol::kGyroScale. */
    int16_t gyro[3];
    /** @brief [16] BMI270 die temperature, scaled by @c ble_protocol::kTempScale. */
    int16_t temperature;
    /** @brief [18] Microphone RMS amplitude of 16-bit PCM, unscaled (0-32767). */
    uint16_t soundLevel;
    /** @brief [20] BMM150 magnetic field along X, Y, Z, scaled by @c ble_protocol::kMagScale. */
    int16_t mag[3];
    /** @brief [26] Battery voltage, in millivolts, unscaled. 0 when unknown. */
    uint16_t batteryMilliVolts;
    /** @brief [28] Battery voltage as a percentage of the regulated (full) voltage. -1 when unknown. */
    int8_t batteryPercent;
    /** @brief [29] Status bits, see @c ble_protocol::kFlagOnBattery and @c kFlagCharging. */
    uint8_t flags;
};

static_assert(sizeof(SensorPacket) == 30, "SensorPacket layout changed, update BLE_PROTOCOL.md and the test client.");
