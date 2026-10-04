/**
 * @file trigger.cpp
 *
 * @brief Implementation of the event triggers: keyword, jolt, button, and manual.
 *
 * @details
 * - Keyword: a cooperative @c Thread polls the NDP for keyword matches every
 *   @c trigger_config::kKeywordPollMs (see @c pollKeywordMatch()).
 * - Jolt: every IMU sample's total acceleration and angular rate are compared
 *   with @c trigger_config::kJoltAccelG / @c kJoltGyroDps, using squared
 *   magnitudes so there's no square root per sample. One impact can exceed
 *   the threshold for several samples, so jolts are rate-limited by @c
 *   trigger_config::kJoltCooldownMs.
 * - Button: a press on @c trigger_config::kButtonPin (to GND, internal
 *   pull-up). An interrupt latches each press, so a short tap isn't missed
 *   while the loop is busy (a flash write can take ~100 ms); the main loop
 *   then triggers, rate-limited by @c trigger_config::kButtonCooldownMs.
 * - Manual: @c triggerManually(), from the client's TRIGGER command.
 *
 * Filtering out everyday motion (running, sports) is not done yet; it needs
 * recorded data to tune against, see README.md.
 *
 */

#include <Arduino.h>
#include <math.h>

#include "trigger.h"
#include "../configs.h"
#include "../logger.h"
#include "../module_utils.h"
#include "../imu/imu.h"
#include "../ndp/ndp_module.h"
#include "../recorder/recorder.h"

/**
 * @defgroup Private
 * Member variables/functions used internally by the trigger module.
 * These are not intended to be used outside of this module.
 * @{
 */
namespace {
/** @brief Jolt threshold on squared acceleration, in squared counts. Set in @c startTriggerModule(). */
float accelThresholdSq = 0.0f;
/** @brief Jolt threshold on squared angular rate, in squared counts. Set in @c startTriggerModule(). */
float gyroThresholdSq = 0.0f;
/** @brief millis() of the last jolt trigger. */
uint32_t lastJoltMs = 0;
/** @brief Whether a jolt has triggered yet (so the cooldown doesn't block the first one). */
bool joltSeen = false;

/** @brief Set by the button interrupt on each press; cleared by @c checkButton(). */
volatile bool buttonPressed = false;
/** @brief millis() of the last button trigger. */
uint32_t lastButtonMs = 0;
/** @brief Whether the button has triggered yet (so the cooldown doesn't block the first press). */
bool buttonSeen = false;

/** @brief Button interrupt (falling edge: pressed). Only latches; the main loop does the rest. */
void onButtonEdge() {
    buttonPressed = true;
}

/**
 * @brief Trigger an event if the button was pressed since the last check.
 *
 * @par Returns
 * Nothing.
 *
 */
void checkButton() {
    if (!buttonPressed) return;
    buttonPressed = false;
    uint32_t now = millis();
    if (buttonSeen && now - lastButtonMs < trigger_config::kButtonCooldownMs) return; // bounce or double press
    buttonSeen = true;
    lastButtonMs = now;
    debug_logs::triggerLogging("Button pressed.");
    triggerEvent(TriggerType::Button, 0, 0.0f, "button");
}

/** @brief Jolt detail value: acceleration crossed its threshold. */
constexpr uint8_t kJoltByAccel = 1;
/** @brief Jolt detail value: angular rate crossed its threshold. */
constexpr uint8_t kJoltByRotation = 2;

/**
 * @brief Sum of squares of a raw 3-axis vector.
 *
 * @param v The vector, in counts.
 *
 * @return Squared magnitude, in squared counts.
 *
 */
float magnitudeSq(const int16_t v[3]) {
    float x = v[0], y = v[1], z = v[2];
    return x * x + y * y + z * z;
}

/**
 * @brief Check one IMU sample for a jolt (an @c ImuSampleHandler).
 *
 * @param sample The sample.
 *
 * @par Returns
 * Nothing.
 *
 */
void checkJolt(const ImuSample& sample) {
    float accelSq = magnitudeSq(sample.accel);
    float gyroSq = magnitudeSq(sample.gyro);
    bool byAccel = accelSq >= accelThresholdSq;
    if (!byAccel && gyroSq < gyroThresholdSq) return;
    if (joltSeen && sample.timestampMs - lastJoltMs < trigger_config::kJoltCooldownMs) return;

    joltSeen = true;
    lastJoltMs = sample.timestampMs;
    float magnitude = byAccel ? sqrtf(accelSq) * imuAccelGPerCount() : sqrtf(gyroSq) * imuGyroDpsPerCount();
    debug_logs::triggerLogging("Jolt: %.1f %s.", magnitude, byAccel ? "g" : "dps");
    triggerEvent(TriggerType::Jolt, byAccel ? kJoltByAccel : kJoltByRotation, magnitude,
                 byAccel ? "jolt-accel" : "jolt-rotation");
}

/**
 * @brief One keyword polling tick: trigger on every new NDP match.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void keywordTick() {
    const char* label = nullptr;
    int index;
    while ((index = pollKeywordMatch(&label)) >= 0) {
        debug_logs::triggerLogging("Keyword %d: %s.", index, label);
        triggerEvent(TriggerType::Keyword, static_cast<uint8_t>(index), 0.0f, label);
    }
}

/** @brief Thread for polling keyword matches. */
Thread keywordThread = makeIdleThread(keywordTick, trigger_config::kKeywordPollMs);

} // namespace
/** @} */ // end of Private

/**
 * @defgroup Public
 * Public API for the trigger module, declared in trigger.h.
 * @{
 */
void startTriggerModule() {
    if (trigger_config::kEnableJolt) {
        float accelCounts = trigger_config::kJoltAccelG / imuAccelGPerCount();
        float gyroCounts = trigger_config::kJoltGyroDps / imuGyroDpsPerCount();
        accelThresholdSq = accelCounts * accelCounts;
        gyroThresholdSq = gyroCounts * gyroCounts;
        addImuSampleHandler(checkJolt);
    }
    keywordThread.enabled = trigger_config::kEnableKeyword && startKeywordDetection();
    if (trigger_config::kEnableButton) {
        pinMode(trigger_config::kButtonPin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(trigger_config::kButtonPin), onButtonEdge, FALLING);
    }

    debug_logs::triggerLogging("Started triggers: keyword %s, jolt %s (%.1f g / %.0f dps), button %s.",
        trigger_config::kEnableKeyword ? "on" : "off", trigger_config::kEnableJolt ? "on" : "off",
        trigger_config::kJoltAccelG, trigger_config::kJoltGyroDps, trigger_config::kEnableButton ? "on" : "off");
}

void updateTriggerModule() {
    runIfDue(keywordThread);
    if (trigger_config::kEnableButton) checkButton();
}

uint16_t triggerManually() {
    debug_logs::triggerLogging("Manual trigger.");
    return triggerEvent(TriggerType::Manual, 0, 0.0f, "manual");
}
/** @} */ // end of Public
