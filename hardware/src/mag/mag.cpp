/**
 * @file mag.cpp
 *
 * @brief Implementation of the BMM150 magnetometer module.
 *
 * @details
 * The BMM150 is wired to the NDP120's SPI bus, so every register access goes
 * through @c NDP.sensorBMM150Read() / @c NDP.sensorBMM150Write(). Its raw
 * output is not in physical units: each axis must be compensated using the
 * chip's factory trim registers and its hall resistance reading. The
 * compensation below is Bosch's floating point reference (BMM150 SensorAPI,
 * @c compensate_x/y/z), producing microtesla.
 *
 */

#include <Arduino.h>
#include <NDP.h>

#include "mag.h"
#include "../ndp/ndp_module.h"
#include "../configs.h"
#include "../logger.h"
#include "../module_utils.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the magnetometer module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/**
 * @defgroup BMM150Registers
 * BMM150 register addresses and values used by this module.
 * @{
 */
constexpr uint8_t kRegChipId = 0x40;
constexpr uint8_t kRegData = 0x42;      // X, Y, Z, RHALL, 8 bytes
constexpr uint8_t kRegPowerCtrl = 0x4B;
constexpr uint8_t kRegOpMode = 0x4C;
constexpr uint8_t kRegRepXY = 0x51;
constexpr uint8_t kRegRepZ = 0x52;
constexpr uint8_t kRegTrimStart = 0x5D; // dig_x1 through dig_xy1, 21 bytes

constexpr uint8_t kChipId = 0x32;
constexpr uint8_t kPowerOn = 0x01;
constexpr uint8_t kOpModeNormal20Hz = 0x28; // ODR 20 Hz (0b101 << 3), normal mode
constexpr uint8_t kRepXYRegular = 0x04;     // 9 repetitions, Bosch "regular" preset
constexpr uint8_t kRepZRegular = 0x0E;      // 15 repetitions, Bosch "regular" preset
/** @} */ // end of BMM150Registers

/** @brief Raw X/Y value the BMM150 reports on overflow. */
constexpr int16_t kOverflowXY = -4096;
/** @brief Raw Z value the BMM150 reports on overflow. */
constexpr int16_t kOverflowZ = -16384;

/** @brief Factory trim values read once from the BMM150 at startup. */
struct Trim {
    int8_t x1, y1, x2, y2, xy2;
    uint8_t xy1;
    int16_t z2, z3, z4;
    uint16_t z1, xyz1;
};

/** @brief This chip's trim values. */
Trim trim = {};

/** @brief The newest valid sample taken by @c magTick(). */
LatestReading<MagReading> latest;

/**
 * @brief Read @p len consecutive BMM150 registers.
 *
 * @param reg First register address.
 * @param len Number of bytes to read, at most 24.
 * @param out Destination for the register values.
 *
 * @return Whether the SPI access through the NDP succeeded.
 *
 */
bool readRegisters(uint8_t reg, uint8_t len, uint8_t* out) {
    uint8_t __attribute__((aligned(4))) buffer[24]; // the NDP writes reads in 4-byte words
    if (len > sizeof(buffer) || NDP.sensorBMM150Read(reg, len, buffer)) return false;
    memcpy(out, buffer, len);
    return true;
}

/**
 * @brief Read and decode the factory trim registers into @c trim.
 *
 * @return Whether the trim registers were read.
 *
 */
bool readTrim() {
    uint8_t t[21]; // t[i] is register 0x5D + i
    if (!readRegisters(kRegTrimStart, sizeof(t), t)) return false;

    trim.x1 = static_cast<int8_t>(t[0x5D - kRegTrimStart]);
    trim.y1 = static_cast<int8_t>(t[0x5E - kRegTrimStart]);
    trim.z4 = static_cast<int16_t>(t[0x62 - kRegTrimStart] | (t[0x63 - kRegTrimStart] << 8));
    trim.x2 = static_cast<int8_t>(t[0x64 - kRegTrimStart]);
    trim.y2 = static_cast<int8_t>(t[0x65 - kRegTrimStart]);
    trim.z2 = static_cast<int16_t>(t[0x68 - kRegTrimStart] | (t[0x69 - kRegTrimStart] << 8));
    trim.z1 = static_cast<uint16_t>(t[0x6A - kRegTrimStart] | (t[0x6B - kRegTrimStart] << 8));
    trim.xyz1 = static_cast<uint16_t>(t[0x6C - kRegTrimStart] | ((t[0x6D - kRegTrimStart] & 0x7F) << 8));
    trim.z3 = static_cast<int16_t>(t[0x6E - kRegTrimStart] | (t[0x6F - kRegTrimStart] << 8));
    trim.xy2 = static_cast<int8_t>(t[0x70 - kRegTrimStart]);
    trim.xy1 = t[0x71 - kRegTrimStart];
    return true;
}

/**
 * @brief Compensate a raw X or Y reading into microtesla.
 *
 * @param raw Raw 13-bit axis value.
 * @param rhall Raw hall resistance from the same sample.
 * @param d1 @c trim.x1 or @c trim.y1.
 * @param d2 @c trim.x2 or @c trim.y2.
 *
 * @return The field in microtesla, or NAN on overflow or bad trim data.
 *
 */
float compensateXY(int16_t raw, uint16_t rhall, int8_t d1, int8_t d2) {
    if (raw == kOverflowXY || rhall == 0 || trim.xyz1 == 0) return NAN;
    float r = (static_cast<float>(trim.xyz1) * 16384.0f / rhall) - 16384.0f;
    float a = trim.xy2 * (r * r / 268435456.0f);
    float b = a + r * trim.xy1 / 16384.0f;
    float c = d2 + 160.0f;
    float d = raw * ((b + 256.0f) * c);
    return ((d / 8192.0f) + (d1 * 8.0f)) / 16.0f;
}

/**
 * @brief Compensate a raw Z reading into microtesla.
 *
 * @param raw Raw 15-bit Z value.
 * @param rhall Raw hall resistance from the same sample.
 *
 * @return The field in microtesla, or NAN on overflow or bad trim data.
 *
 */
float compensateZ(int16_t raw, uint16_t rhall) {
    if (raw == kOverflowZ || trim.z2 == 0 || trim.z1 == 0 || trim.xyz1 == 0 || rhall == 0) return NAN;
    float a = static_cast<float>(raw) - trim.z4;
    float b = static_cast<float>(rhall) - trim.xyz1;
    float c = trim.z3 * b;
    float d = trim.z1 * static_cast<float>(rhall) / 32768.0f;
    float e = trim.z2 + d;
    float f = (a * 131072.0f) - c;
    return (f / (e * 4.0f)) / 16.0f;
}

/**
 * @brief Power the BMM150 up and configure it, once.
 *
 * @return Whether the BMM150 answered with its chip ID and was configured.
 *
 */
bool initAttempt() {
    // The BMM150 only answers SPI once its power control bit is set.
    if (NDP.sensorBMM150Write(kRegPowerCtrl, kPowerOn)) return false;
    delay(20);

    uint8_t chipId = 0;
    if (!readRegisters(kRegChipId, 1, &chipId)) return false;
    if (chipId != kChipId) {
        debug_logs::magLogging("Unexpected BMM150 chip ID 0x%02X (expected 0x%02X).", chipId, kChipId);
        return false;
    }

    return readTrim() &&
        NDP.sensorBMM150Write(kRegRepXY, kRepXYRegular) == 0 &&
        NDP.sensorBMM150Write(kRegRepZ, kRepZRegular) == 0 &&
        NDP.sensorBMM150Write(kRegOpMode, kOpModeNormal20Hz) == 0;
}

/**
 * @brief One cooperative thread tick, sampling and compensating the BMM150.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void magTick() {
    uint8_t d[8];
    if (!readRegisters(kRegData, sizeof(d), d)) {
        debug_logs::magLogging("Failed to read BMM150 sample.");
        return;
    }

    // X/Y are 13-bit and Z 15-bit signed, left aligned, with flag bits in the low bits of each LSB.
    int16_t rawX = static_cast<int16_t>((static_cast<int8_t>(d[1]) * 32) | (d[0] >> 3));
    int16_t rawY = static_cast<int16_t>((static_cast<int8_t>(d[3]) * 32) | (d[2] >> 3));
    int16_t rawZ = static_cast<int16_t>((static_cast<int8_t>(d[5]) * 128) | (d[4] >> 1));
    uint16_t rhall = static_cast<uint16_t>((d[7] << 6) | (d[6] >> 2));

    float x = compensateXY(rawX, rhall, trim.x1, trim.x2);
    float y = compensateXY(rawY, rhall, trim.y1, trim.y2);
    float z = compensateZ(rawZ, rhall);
    if (isnan(x) || isnan(y) || isnan(z)) return; // overflow, keep the previous good sample

    latest.store(MagReading{{x, y, z}, millis()});
}

/**
 * @brief One logging tick, queuing the newest sample.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void magLogTick() {
    if (!latest.valid()) return;
    const MagReading& r = latest.value();
    debug_logs::magLogging("Field [%.1f, %.1f, %.1f] uT", r.field[0], r.field[1], r.field[2]);
}

/** @brief Thread for sampling the BMM150. */
Thread magThread = makeIdleThread(magTick, mag_config::kThreadRefreshIntervalMs);
/** @brief Thread for periodically logging the newest sample. */
Thread magLogThread = makeIdleThread(magLogTick, debug_config::kMagLoopDelay);

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the magnetometer module, declared in mag.h.
 * @{
 */
bool startMagModule() {
    if (!isNDPReady()) {
        debug_logs::magLogging("NDP is not ready, the BMM150 is unreachable.");
        return false;
    }

    uint8_t attempts = 0;
    while (!initAttempt()) {
        if (++attempts >= mag_config::kInitAttempts) {
            debug_logs::magLogging("BMM150 failed to initialize after %u attempts.", attempts);
            return false;
        }
        debug_logs::magLogging("BMM150 init attempt %u failed. Retrying...", attempts);
    }

    magThread.enabled = true;
    magLogThread.enabled = true;

    debug_logs::magLogging("Started magnetometer module (20 Hz, regular preset).");
    return true;
}

void updateMagModule() {
    runIfDue(magThread);
    runIfDue(magLogThread);
}

bool getLatestMagReading(MagReading& reading) {
    return latest.copyTo(reading);
}
/** @} */ // end of Public
