/**
 * @file hardware.ino
 *
 * @brief Application entry point wiring together every firmware module.
 *
 * @details
 * Firmware for the Arduino Nicla Voice (ABX00061) as a personal-safety
 * recorder. It records audio and motion continuously into a rolling buffer on
 * the external flash. A trigger (a keyword, a jolt of movement, or a manual
 * command) saves the last ~10 s plus the next 50 s as an event, which a
 * client fetches over BLE.
 *
 * @c setup() brings up the Nicla's power management, then the NDP120 (which
 * the onboard IMU and microphone hang off), BLE (after the NDP, for heap
 * reasons, see @c startModules()), the BMI270 IMU, the microphone, the recorder (which needs the NDP
 * to have finished with the flash), and the triggers. @c loop() then ticks
 * every module once per iteration. Everything that touches the shared SPI bus
 * (NDP, IMU, flash) runs on this one thread.
 *
 * If a module fails to start, a normal build waits @c
 * main_config::kFailureRebootDelayMs and reboots to retry. A debug build (@c
 * NICLA_DEBUG = 1) instead stays in the failed state, showing it on the status
 * LED and serial log, and flushes queued debug logs via @c
 * debug_logs::flushLogs().
 *
 */

#include <Arduino.h>
#include <Nicla_System.h>

#include "src/configs.h"
#include "src/logger.h"
#include "src/led/led_handler.h"
#include "src/ndp/ndp_module.h"
#include "src/imu/imu.h"
#include "src/audio/audio.h"
#include "src/recorder/recorder.h"
#include "src/trigger/trigger.h"
#include "src/ble/ble.h"
#include "src/module_utils.h"

namespace {
#if NICLA_DEBUG
/** @brief Thread printing the queued debug logs every @c debug_config::kLoopLogDelay. */
Thread logFlushThread = Thread([]() { debug_logs::flushLogs(); }, debug_config::kLoopLogDelay);
#endif

/**
 * @brief Talk to the PMIC so its I2C watchdog doesn't reset its settings.
 *
 * @details
 * The BQ25120A reverts its registers to defaults if nothing talks to it for
 * ~50 s. nicla::begin() normally runs a thread for this; reading the status
 * register (the same read that thread does) from the main loop is enough.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void pingPmic() {
  nicla::getOperatingStatus();
}

/** @brief Thread pinging the PMIC every @c main_config::kPmicPingIntervalMs, started in setup(). */
Thread pmicPingThread = makeIdleThread(pingPmic, main_config::kPmicPingIntervalMs);

/**
 * @brief Flush queued logs if the flush interval has elapsed (debug builds only).
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void flushLogsIfDue() {
#if NICLA_DEBUG
  runIfDue(logFlushThread);
#endif
}

#if NICLA_DEBUG
/**
 * @brief Why the chip last reset, as the nRF52's RESETREAS register records it.
 *
 * @details
 * The register accumulates until cleared, so it's cleared after reading.
 * No bits set means power-on (or a power dip): a crash would show as a
 * software reset (mbed's error handler resets the chip) or a lockup.
 *
 * @return A short description.
 *
 */
const char* readResetReason() {
  uint32_t reason = NRF_POWER->RESETREAS;
  NRF_POWER->RESETREAS = reason; // write 1s to clear
  if (reason & POWER_RESETREAS_LOCKUP_Msk) return "CPU lockup (crash)";
  if (reason & POWER_RESETREAS_DOG_Msk) return "watchdog";
  if (reason & POWER_RESETREAS_SREQ_Msk) return "software reset (crash report or failure reboot)";
  if (reason & POWER_RESETREAS_RESETPIN_Msk) return "reset pin or button";
  if (reason != 0) return "debugger or wake-up";
  return "power-on or power dip";
}

#endif

/**
 * @brief Grow the heap to its full size up front.
 *
 * @details
 * The C library grows the heap on demand, in page-sized steps. Near the end
 * of RAM a whole step may not fit even when the memory BLE asks for does, so
 * BLE's one 13 KB allocation can fail with memory to spare (the
 * "_stack_buffer != NULL" crash). Allocating the largest block that fits once
 * and freeing it makes the heap claim all of its memory now; later
 * allocations then come from that free space without growing the heap.
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void claimWholeHeap() {
  size_t low = 0, high = 64 * 1024;
  while (low < high) {  // binary search for the largest malloc that succeeds
    size_t mid = (low + high + 1) / 2;
    void* block = malloc(mid);
    if (block != nullptr) {
      free(block);
      low = mid;
    } else {
      high = mid - 1;
    }
  }
}

/**
 * @brief Keep the C library's stdio allocations out of BLE's way.
 *
 * @details
 * The NDP library prints with @c printf and reads its firmware with @c fopen.
 * Both make stdio allocate memory it never frees: a 1 KB @c stdout buffer, and
 * a block of @c FILE slots on the first @c fopen. Allocated during the NDP
 * load, they split the heap so BLE's 13 KB block no longer fits. So @c stdout
 * is made unbuffered (no buffer at all), and the @c FILE slots are allocated
 * now, while the heap is still empty, by opening a file that doesn't exist
 * (the slots are allocated before the open fails).
 *
 * @par Parameters
 * None.
 *
 * @par Returns
 * Nothing.
 *
 */
void settleStdioAllocations() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  FILE* none = fopen("/none/none", "rb");
  if (none != nullptr) fclose(none);
}

/**
 * @brief Start every module in order.
 *
 * @return @c BlinkState::Idle if everything started, else the failed module's state.
 *
 */
BlinkState startModules() {
  // Logs are flushed after each step: the log queue is small, and setup is long.
  // The heap is only ~21 KB, and BLE keeps ~15 KB of it once started (one
  // 13 KB block plus its buffers), which leaves too little for the NDP firmware
  // load's file buffers. So the NDP loads first (it takes ~13 s): its buffers
  // are temporary and merge back into one free block when freed, leaving room
  // for BLE's block (see settleStdioAllocations()). The IMU and microphone
  // sit behind the NDP.
  if (!startNDPModule()) return BlinkState::NDPFail;
  debug_logs::flushLogs();
  if (!startBLEModule()) return BlinkState::BLEFail;
  debug_logs::flushLogs();
  if (!startIMUModule()) return BlinkState::IMUFail;
  if (!startAudioModule()) return BlinkState::AudioFail;
  debug_logs::flushLogs();

  // The recorder mounts the flash the NDP library has just released.
  if (!startRecorder()) return BlinkState::StorageFail;
  addImuSampleHandler(recordMotionSample);
  setAudioChunkHandler(recordAudioChunk);
  setEventHandlers(notifyEventStarted, notifyEventReady);
  startTriggerModule();
  debug_logs::flushLogs();
  return BlinkState::Idle;
}
}

void setup() {
#if NICLA_DEBUG
  // Why the last run ended (a crash, a power dip, ...), logged once Serial is up.
  const char* resetReason = readResetReason();
#endif
  // Heap setup for BLE's 13 KB block, before anything else allocates.
  claimWholeHeap();
  settleStdioAllocations();

  // Serial carries the debug log, so it's only started in debug builds. Don't
  // wait forever when no monitor is attached (e.g. running untethered).
  if (debug_config::kEnableVerboseLogging) {
    Serial.begin(115200);
    unsigned long serialStart = millis();
    while (!Serial && millis() - serialStart < debug_config::kSerialWaitTimeoutMs) delay(10);
  }

  // The I2C bus the LED driver and PMIC share. nicla::begin() isn't used: it
  // also starts a thread whose 768-byte stack comes off the heap, and BLE
  // needs every byte (see startModules()). pingPmic() replaces that thread.
  // The PMIC's LDO powers the header pins' level shifters: on (1.8 V) for the
  // button (a header pin, trigger_config::kButtonPin), else off to save power.
  Wire1.begin();
  nicla::started = true;
  if (trigger_config::kEnableButton) {
    nicla::enable1V8LDO();
  } else {
    nicla::disableLDO();
  }
  pmicPingThread.enabled = true;

  // The status LED (debug builds) runs on its own thread, indicating state without blocking other operations.
  startStatusLED();
  setStatusState(BlinkState::Setup);
  updateStatusLED();
#if NICLA_DEBUG
  debug_logs::mainLogging("Reset reason: %s.", resetReason);
  debug_logs::flushLogs();
#endif

  BlinkState result = startModules();
  if (result != BlinkState::Idle) setStatusState(result);

  unsigned long failedAt = millis();
  while (inFailedState()) {
    runIfDue(pmicPingThread);
    updateStatusLED();
    flushLogsIfDue();
    if (!debug_config::kDebugBuild && millis() - failedAt >= main_config::kFailureRebootDelayMs) {
      NVIC_SystemReset(); // retry from a clean boot
    }
    delay(main_config::kRefreshIntervalMs);
  }

  setStatusState(BlinkState::Idle);
}

void loop() {
  runIfDue(pmicPingThread);
  updateStatusLED();
  updateAudioModule();
  updateIMUModule();
  updateTriggerModule();
  updateRecorder();
  updateBLEModule();

  flushLogsIfDue();
  delay(main_config::kRefreshIntervalMs);
}
