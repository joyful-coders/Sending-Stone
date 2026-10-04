/**
 * @headerfile battery.h "src/battery/battery.h"
 *
 */

#pragma once

#include <cstdint>

/** @brief One reading from the BQ25120A PMIC's battery monitor. */
struct BatteryReading {
    /** @brief Estimated battery voltage, in millivolts. 0 when it could not be determined. */
    uint16_t milliVolts;
    /**
     * @brief Battery voltage as a percentage of the regulated (full) voltage, 60 to 100.
     *
     * @note
     * Not a state of charge: for a 4.2 V LiPo, below ~84% is empty. -1 when it could not be determined.
     */
    int8_t percent;
    /** @brief Whether the board is running from the battery rather than USB / VIN. */
    bool onBattery;
    /** @brief Whether the PMIC reports the battery is currently charging. */
    bool charging;
    /** @brief millis() timestamp the reading was taken at. */
    unsigned long timestampMs;
};

/**
 * @brief Start the battery module.
 *
 * @details
 * Takes an initial reading so @c getLatestBatteryReading() has data right
 * away, then starts the module's polling thread. Must run after @c
 * nicla::begin(). Readings with no battery attached are reported as-is (the
 * PMIC returns meaningless values), check @c BatteryReading::onBattery.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void startBatteryModule();

/**
 * @brief Run the battery polling thread tick, if it is due.
 *
 * @details
 * Call regularly from the main loop. Once every @c
 * battery_config::kThreadRefreshIntervalMs, reads the PMIC into the module's
 * latest reading and logs it.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void updateBatteryModule();

/**
 * @brief Copy out the most recent battery reading.
 *
 * @param reading Destination that receives the latest reading.
 *
 * @return Whether a reading was available.
 * @retval true @p reading was filled with the latest reading.
 * @retval false The module has not started.
 *
 */
bool getLatestBatteryReading(BatteryReading& reading);
