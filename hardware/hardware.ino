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
#include <malloc.h>

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

/**
 * @brief Log the largest block that could be allocated right now, then flush the log (debug builds only).
 *
 * @details
 * RAM is the tightest resource on this board: BLE alone needs one 13 KB
 * block. Logging the headroom after each setup step shows exactly where it
 * goes. Flushing between steps also keeps the small log queue from dropping
 * setup messages.
 *
 * @param step Name of the step just completed.
 *
 * @par Returns
 * Nothing.
 *
 */
#if NICLA_DEBUG
/** @brief Heap state at one point in setup. */
struct HeapSnapshot {
  size_t largest; ///< largest block malloc could return
  size_t inUse;   ///< bytes currently allocated
  size_t arena;   ///< bytes the heap has grown to so far
};

/** @brief Measure the heap right now. */
HeapSnapshot takeHeapSnapshot() {
  struct mallinfo info = mallinfo();  // before the probe, which would grow the arena
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
  return {low, static_cast<size_t>(info.uordblks), static_cast<size_t>(info.arena)};
}

/** @brief Log a snapshot taken after @p step. */
void logHeapSnapshot(const char* step, const HeapSnapshot& heap) {
  debug_logs::mainLogging("After %s: largest free %u B, in use %u B, arena %u B.",
                          step, heap.largest, heap.inUse, heap.arena);
  debug_logs::flushLogs();
}
#endif

void reportMemory(const char* step) {
#if NICLA_DEBUG
  logHeapSnapshot(step, takeHeapSnapshot());
#else
  (void)step;
#endif
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
  reportMemory("status LED");

  // The heap is only ~21 KB, and BLE keeps ~15 KB of it once started (one
  // 13 KB block plus its buffers), which leaves too little for the NDP firmware
  // load's file buffers. So the NDP loads first (it takes ~13 s): its buffers
  // are temporary and merge back into one free block when freed, leaving room
  // for BLE's block (see settleStdioAllocations()). The IMU and microphone
  // sit behind the NDP.
  if (!startNDPModule()) return BlinkState::NDPFail;
  reportMemory("NDP");
  if (!startBLEModule()) return BlinkState::BLEFail;
  reportMemory("BLE");
  if (!startIMUModule()) return BlinkState::IMUFail;
  if (!startAudioModule()) return BlinkState::AudioFail;
  reportMemory("IMU + audio");

  // The recorder mounts the flash the NDP library has just released.
  if (!startRecorder()) return BlinkState::StorageFail;
  addImuSampleHandler(recordMotionSample);
  setAudioChunkHandler(recordAudioChunk);
  setEventHandlers(notifyEventStarted, notifyEventReady);
  startTriggerModule();
  reportMemory("recorder + triggers");
  return BlinkState::Idle;
}
}

void setup() {
#if NICLA_DEBUG
  // Heap use before anything runs (static constructors), logged once Serial is up.
  HeapSnapshot heapAtStart = takeHeapSnapshot();
#endif
  settleStdioAllocations();

  // Serial carries the debug log, so it's only started in debug builds. Don't
  // wait forever when no monitor is attached (e.g. running untethered).
  if (debug_config::kEnableVerboseLogging) {
    Serial.begin(115200);
    unsigned long serialStart = millis();
    while (!Serial && millis() - serialStart < debug_config::kSerialWaitTimeoutMs) delay(10);
  }
#if NICLA_DEBUG
  HeapSnapshot heapAfterSerial = takeHeapSnapshot();
#endif

  // Power management and the I2C bus the LED driver and PMIC share. The header
  // pins' LDO is unused (no external hardware), so it's turned off to save power.
  nicla::begin();
  nicla::disableLDO();
#if NICLA_DEBUG
  HeapSnapshot heapAfterNicla = takeHeapSnapshot();
#endif

  // The status LED (debug builds) runs on its own thread, indicating state without blocking other operations.
  startStatusLED();
  setStatusState(BlinkState::Setup);
  updateStatusLED();
#if NICLA_DEBUG
  logHeapSnapshot("static init", heapAtStart);
  logHeapSnapshot("Serial", heapAfterSerial);
  logHeapSnapshot("nicla::begin", heapAfterNicla);
#endif

  BlinkState result = startModules();
  if (result != BlinkState::Idle) setStatusState(result);

  unsigned long failedAt = millis();
  while (inFailedState()) {
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
  updateStatusLED();
  updateAudioModule();
  updateIMUModule();
  updateTriggerModule();
  updateRecorder();
  updateBLEModule();

  flushLogsIfDue();
  delay(main_config::kRefreshIntervalMs);
}
