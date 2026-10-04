/**
 * @file imu.cpp
 *
 * @brief Implementation of the BMI270 IMU module.
 *
 * @details
 * The BMI270 is wired to the NDP120's SPI bus, so every register access goes
 * through @c NDP.sensorBMI270Read() / @c NDP.sensorBMI270Write(). The startup
 * sequence follows the core's NDP SensorTest example. A cooperative @c Thread
 * then samples it every @c imu_config::kThreadRefreshIntervalMs with a single
 * burst read covering acceleration and angular rate, and hands each raw sample
 * to the registered handlers (the recorder and the jolt trigger).
 *
 */

#include <Arduino.h>
#include <NDP.h>

#include "imu.h"
#include "bmi270_config.h"
#include "../ndp/ndp_module.h"
#include "../configs.h"
#include "../logger.h"
#include "../module_utils.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the IMU module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/**
 * @defgroup BMI270Registers
 * BMI270 register addresses and values used by this module.
 * @{
 */
constexpr uint8_t kRegChipId = 0x00;
constexpr uint8_t kRegData = 0x0C;           // ACC X/Y/Z then GYR X/Y/Z, 12 bytes
constexpr uint8_t kRegInternalStatus = 0x21;
constexpr uint8_t kRegAccConf = 0x40;
constexpr uint8_t kRegAccRange = 0x41;
constexpr uint8_t kRegGyrConf = 0x42;
constexpr uint8_t kRegGyrRange = 0x43;
constexpr uint8_t kRegInitCtrl = 0x59;
constexpr uint8_t kRegInitData = 0x5E;
constexpr uint8_t kRegPwrConf = 0x7C;
constexpr uint8_t kRegPwrCtrl = 0x7D;
constexpr uint8_t kRegCmd = 0x7E;

constexpr uint8_t kChipId = 0x24;
constexpr uint8_t kCmdSoftReset = 0xB6;
constexpr uint8_t kInitOk = 0x01;
constexpr uint8_t kAccConf = 0xA8;           // 100 Hz ODR, normal averaging, performance mode
constexpr uint8_t kGyrConf = 0xA9;           // 200 Hz ODR, normal filter, performance mode
constexpr uint8_t kPwrCtrlAccGyr = 0x06;     // accelerometer and gyroscope on (temperature sensor off)
constexpr uint8_t kPwrConfFastPowerUp = 0x02;

/** @brief Bytes in one burst: accelerometer then gyroscope X/Y/Z. */
constexpr uint8_t kBurstLength = 12;
/** @brief Offset of the gyroscope data within the burst. */
constexpr uint8_t kGyroOffset = 6;
/** @} */ // end of BMI270Registers

/**
 * @brief ACC_RANGE register value for a range in g.
 *
 * @param rangeG One of 2, 4, 8, or 16.
 *
 * @return The register value, or 0xFF for an unsupported range.
 *
 */
constexpr uint8_t accelRangeBits(uint8_t rangeG) {
    return rangeG == 2 ? 0 : rangeG == 4 ? 1 : rangeG == 8 ? 2 : rangeG == 16 ? 3 : 0xFF;
}

/**
 * @brief GYR_RANGE register value for a range in degrees per second.
 *
 * @param rangeDps One of 125, 250, 500, 1000, or 2000.
 *
 * @return The register value, or 0xFF for an unsupported range.
 *
 */
constexpr uint8_t gyroRangeBits(uint16_t rangeDps) {
    return rangeDps == 2000 ? 0 : rangeDps == 1000 ? 1 : rangeDps == 500 ? 2 :
           rangeDps == 250 ? 3 : rangeDps == 125 ? 4 : 0xFF;
}

static_assert(accelRangeBits(imu_config::kAccelRangeG) != 0xFF, "imu_config::kAccelRangeG must be 2, 4, 8, or 16.");
static_assert(gyroRangeBits(imu_config::kGyroRangeDps) != 0xFF, "imu_config::kGyroRangeDps must be 125, 250, 500, 1000, or 2000.");

/** @brief g per accelerometer count at the configured range. */
constexpr float kAccelGPerCount = imu_config::kAccelRangeG / 32768.0f;
/** @brief Degrees per second per gyroscope count at the configured range. */
constexpr float kGyroDpsPerCount = imu_config::kGyroRangeDps / 32768.0f;

/** @brief Maximum number of sample handlers. */
constexpr uint8_t kMaxHandlers = 4;
/** @brief Registered sample handlers, see @c addImuSampleHandler(). */
ImuSampleHandler handlers[kMaxHandlers] = {};
/** @brief Number of entries used in @c handlers. */
uint8_t handlerCount = 0;

/** @brief The newest sample, kept only for the debug log. */
LatestReading<ImuSample> latest;

/**
 * @brief Decode a little-endian signed 16-bit value.
 *
 * @param p Pointer to the low byte.
 *
 * @return The decoded value.
 *
 */
inline int16_t readInt16(const uint8_t* p) {
    return static_cast<int16_t>(p[0] | (p[1] << 8));
}

/**
 * @brief Read one BMI270 register.
 *
 * @param reg Register address.
 * @param value Destination for the register's value.
 *
 * @return Whether the SPI access through the NDP succeeded.
 *
 */
bool readRegister(uint8_t reg, uint8_t& value) {
    uint8_t __attribute__((aligned(4))) buffer[4]; // the NDP writes reads in 4-byte words
    if (NDP.sensorBMI270Read(reg, 1, buffer)) return false;
    value = buffer[0];
    return true;
}

/**
 * @brief Read and discard one register, which (re)selects the BMI270's SPI mode.
 *
 * @details
 * The BMI270 powers up, and comes back from a soft reset, in I2C mode. Its
 * first SPI access only switches the interface over, so that read is junk.
 *
 * @return Whether the SPI access through the NDP succeeded.
 *
 */
bool selectSpiMode() {
    uint8_t ignored;
    return readRegister(kRegChipId, ignored);
}

/**
 * @brief Soft reset the BMI270 and upload its configuration blob, once.
 *
 * @return Whether the BMI270 reported a successful initialization.
 *
 */
bool initAttempt() {
    uint8_t value = 0;
    if (!selectSpiMode() || !readRegister(kRegChipId, value)) return false;
    if (value != kChipId) {
        debug_logs::imuLogging("Unexpected BMI270 chip ID 0x%02X (expected 0x%02X).", value, kChipId);
        return false;
    }

    if (NDP.sensorBMI270Write(kRegCmd, kCmdSoftReset)) return false;
    delay(20);
    if (!selectSpiMode()) return false;

    // Disable advanced power save, then upload the config blob.
    if (NDP.sensorBMI270Write(kRegPwrConf, 0x00)) return false;
    delay(20);
    if (NDP.sensorBMI270Write(kRegInitCtrl, 0x00)) return false;
    delay(200);
    if (NDP.sensorBMI270Write(kRegInitData, sizeof(bmi270_maximum_fifo_config_file),
                              const_cast<uint8_t*>(bmi270_maximum_fifo_config_file))) return false;
    if (NDP.sensorBMI270Write(kRegInitCtrl, 0x01)) return false;
    delay(200);

    return readRegister(kRegInternalStatus, value) && value == kInitOk;
}

/**
 * @brief One cooperative thread tick, sampling the BMI270 in a single burst read.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void imuTick() {
    uint8_t __attribute__((aligned(4))) data[kBurstLength];
    if (NDP.sensorBMI270Read(kRegData, kBurstLength, data)) {
        debug_logs::imuLogging("Failed to read BMI270 sample.");
        return;
    }

    ImuSample sample;
    for (uint8_t i = 0; i < 3; ++i) {
        sample.accel[i] = readInt16(&data[2 * i]);
        sample.gyro[i] = readInt16(&data[kGyroOffset + 2 * i]);
    }
    sample.timestampMs = millis();

    if (debug_config::kEnableIMULogging) latest.store(sample);
    for (uint8_t i = 0; i < handlerCount; ++i) handlers[i](sample);
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
void imuLogTick() {
    if (!latest.valid()) return;
    const ImuSample& r = latest.value();
    debug_logs::imuLogging("Accel [%.2f, %.2f, %.2f] g Gyro [%.0f, %.0f, %.0f] dps",
        r.accel[0] * kAccelGPerCount, r.accel[1] * kAccelGPerCount, r.accel[2] * kAccelGPerCount,
        r.gyro[0] * kGyroDpsPerCount, r.gyro[1] * kGyroDpsPerCount, r.gyro[2] * kGyroDpsPerCount);
}

/** @brief Thread for sampling the BMI270. */
Thread imuThread = makeIdleThread(imuTick, imu_config::kThreadRefreshIntervalMs);
/** @brief Thread for periodically logging the newest sample. */
Thread imuLogThread = makeIdleThread(imuLogTick, debug_config::kIMULoopDelay);

} // namespace
/** @} */ // end of Private

// The 4-byte rounding of NDP reads must stay inside the burst buffer.
static_assert(kBurstLength % 4 == 0, "BMI270 burst buffer must be a multiple of 4 bytes.");

/**
 * @defgroup Public
 * Public API for the IMU module, declared in imu.h.
 * @{
 */
bool startIMUModule() {
    if (!isNDPReady()) {
        debug_logs::imuLogging("NDP is not ready, the BMI270 is unreachable.");
        return false;
    }

    uint8_t attempts = 0;
    while (!initAttempt()) {
        if (++attempts >= imu_config::kInitAttempts) {
            debug_logs::imuLogging("BMI270 failed to initialize after %u attempts.", attempts);
            return false;
        }
        debug_logs::imuLogging("BMI270 init attempt %u failed. Retrying...", attempts);
    }

    bool configured =
        NDP.sensorBMI270Write(kRegPwrCtrl, kPwrCtrlAccGyr) == 0 &&
        NDP.sensorBMI270Write(kRegAccConf, kAccConf) == 0 &&
        NDP.sensorBMI270Write(kRegAccRange, accelRangeBits(imu_config::kAccelRangeG)) == 0 &&
        NDP.sensorBMI270Write(kRegGyrConf, kGyrConf) == 0 &&
        NDP.sensorBMI270Write(kRegGyrRange, gyroRangeBits(imu_config::kGyroRangeDps)) == 0 &&
        NDP.sensorBMI270Write(kRegPwrConf, kPwrConfFastPowerUp) == 0;
    if (!configured) {
        debug_logs::imuLogging("Failed to configure BMI270 ranges.");
        return false;
    }

    imuThread.enabled = true;
    imuLogThread.enabled = true;

    debug_logs::imuLogging("Started IMU module (accel +-%u g, gyro +-%u dps).",
        imu_config::kAccelRangeG, imu_config::kGyroRangeDps);
    return true;
}

void updateIMUModule() {
    runIfDue(imuThread);
    runIfDue(imuLogThread);
}

bool addImuSampleHandler(ImuSampleHandler handler) {
    if (handler == nullptr || handlerCount >= kMaxHandlers) return false;
    handlers[handlerCount++] = handler;
    return true;
}

float imuAccelGPerCount() {
    return kAccelGPerCount;
}

float imuGyroDpsPerCount() {
    return kGyroDpsPerCount;
}
/** @} */ // end of Public
