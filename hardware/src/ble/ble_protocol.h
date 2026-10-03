/**
 * @file ble_protocol.h
 * @headerfile ble_protocol.h "src/ble/ble_protocol.h"
 *
 * @brief Wire format of the sensor packet streamed over BLE.
 *
 * @details
 * This is the contract shared by @c ble.cpp and any receiving client. Every
 * notification on the readings characteristic carries exactly one @c
 * SensorPacket: 20 bytes, packed, little-endian, with every value sent as a
 * scaled whole number. Divide each field by its @c k*Scale constant below to
 * recover the real-world value. See BLE_PROTOCOL.md for the client-side
 * decoding guide.
 *
 * @note
 * Any change to this layout must bump @c BLE_PACKET_VERSION and update
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
 * @brief One combined sample of every sensor, sent once per notification.
 *
 * @details
 * Packed with no padding and stored little-endian (the ESP32-C6's native byte
 * order). Byte offsets are fixed, see the table in BLE_PROTOCOL.md.
 *
 */
struct __attribute__((packed)) SensorPacket
{
    /** @brief Device uptime, in milliseconds, when the packet was built. Wraps after ~49.7 days. */
    uint32_t timestampMs;
    /** @brief Acceleration along X, Y, Z, scaled by @c ble_protocol::kAccelScale. */
    int16_t accel[3];
    /** @brief Angular rate around X, Y, Z, scaled by @c ble_protocol::kGyroScale. */
    int16_t gyro[3];
    /** @brief MPU6050 die temperature, scaled by @c ble_protocol::kTempScale. */
    int16_t temperature;
    /** @brief Raw averaged ADC level from the sound sensor, unscaled (0-4095). */
    uint16_t soundLevel;
};

static_assert(sizeof(SensorPacket) == 20, "SensorPacket must fit a default 23-byte ATT MTU (20-byte payload).");
