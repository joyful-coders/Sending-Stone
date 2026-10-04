/**
 * @file battery.cpp
 *
 * @brief Implementation of the battery monitor module.
 *
 * @details
 * Polls the BQ25120A PMIC through the core's @c nicla API. The PMIC shares the
 * I2C bus with the RGB LED driver and battery state changes slowly, so it is
 * read every @c battery_config::kThreadRefreshIntervalMs rather than at the
 * sensor rate.
 *
 */

#include <Arduino.h>
#include <Nicla_System.h>

#include "battery.h"
#include "../configs.h"
#include "../logger.h"
#include "../module_utils.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the battery module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief The newest reading taken by @c batteryTick(). */
LatestReading<BatteryReading> latest;

/**
 * @brief One cooperative thread tick, reading the PMIC and logging the result.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void batteryTick() {
    float volts = nicla::getCurrentBatteryVoltage();
    BatteryReading r;
    r.milliVolts = volts > 0.0f ? static_cast<uint16_t>(volts * 1000.0f + 0.5f) : 0;
    r.percent = nicla::getBatteryVoltagePercentage();
    r.onBattery = nicla::runsOnBattery();
    r.charging = nicla::getOperatingStatus() == OperatingStatus::Charging;
    r.timestampMs = millis();
    latest.store(r);

    debug_logs::batteryLogging("%u mV (%d%%), %s%s", r.milliVolts, r.percent,
        r.onBattery ? "on battery" : "on USB/VIN", r.charging ? ", charging" : "");
}

/** @brief Thread for polling the PMIC. */
Thread batteryThread = makeIdleThread(batteryTick, battery_config::kThreadRefreshIntervalMs);

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the battery module, declared in battery.h.
 * @{
 */
void startBatteryModule() {
    batteryTick();
    batteryThread.enabled = true;
}

void updateBatteryModule() {
    runIfDue(batteryThread);
}

bool getLatestBatteryReading(BatteryReading& reading) {
    return latest.copyTo(reading);
}
/** @} */ // end of Public
