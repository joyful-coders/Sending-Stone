/**
 * @file ndp_provision.ino
 *
 * @brief Puts the NDP120 firmware packages onto the Nicla Voice's external flash.
 *
 * @details
 * A drop-in replacement for the core's NDP > Syntiant_upload_fw_ymodem example,
 * speaking the same one-letter serial protocol so Arduino's syntiant-uploader
 * works unchanged, with two differences:
 *
 * - The NDP120 shares the external flash's SPI bus. The example never touches
 *   it, so after a reset its chip-select floats and it can corrupt flash
 *   traffic. This sketch holds the NDP in reset with its chip-select inactive.
 * - It reports what it's doing: flash init/mount results at boot, and the
 *   result of every format and file listing, on Serial and on the RGB LED
 *   (yellow = ready, not mounted yet; green = mounted; red = mount failed;
 *   blue = busy).
 *
 * It never mounts the flash at boot, only on command, so a damaged filesystem
 * can't stop it reaching its command loop. Boot prints a line per step and the
 * nRF52's reset reason, so a silent board can be traced.
 *
 * Serial commands (115200 baud):
 *   I -> print flash/mount status
 *   M -> mount the flash, prints "MOUNT OK" or "MOUNT FAILED <err>"
 *   F -> format the external flash, prints "FORMAT OK" or "FORMAT FAILED <err>"
 *   L -> list files, ends with "END"
 *   S -> list files with size and sha256, ends with "END"
 *   Y -> receive a file over YMODEM (prints "Y" first)
 *
 * Used by tools/upload-ndp-firmware.ps1, see README.md.
 *
 */

#include <Nicla_System.h>
#include "SPIFBlockDevice.h"
#include "LittleFileSystem.h"
#include "sha256.h"
#include "ymodem.h"

/** @brief NDP120 chip-select, shared SPI bus with the flash (see NDP.h). */
constexpr PinName kNdpCs = p31;
/** @brief NDP120 active-low power-on reset (see NDP.h). */
constexpr PinName kNdpReset = p18;

SPIFBlockDevice spif(SPI_PSELMOSI0, SPI_PSELMISO0, SPI_PSELSCK0, CS_FLASH, 16000000);
mbed::LittleFileSystem fs("fs");

/** @brief Whether the LittleFS on the external flash is currently mounted. */
bool mounted = false;
/** @brief Result of the last @c spif.init(), 0 on success. */
int initResult = -1;
/** @brief Result of the last mount attempt, 0 on success. */
int mountResult = -1;

char filename[256] = {'\0'};
mbedtls_sha256_context ctx;

/**
 * @brief Show flash state on the RGB LED: green mounted, red not mounted.
 */
void showMountState() {
  nicla::leds.setColor(mounted ? green : red);
}

/**
 * @brief Print the flash init and mount results.
 */
void printStatus() {
  Serial.print("FLASH init=");
  Serial.print(initResult);
  Serial.print(" size=");
  Serial.print((unsigned long)spif.size());
  Serial.print(" mount=");
  Serial.print(mountResult);
  Serial.println(mounted ? " MOUNTED" : " NOT MOUNTED");
}

/**
 * @brief Mount the flash if it isn't already, print and return the result.
 */
bool mountFlash() {
  if (!mounted) {
    nicla::leds.setColor(blue);
    Serial.println("MOUNTING");
    mountResult = fs.mount(&spif);
    mounted = mountResult == 0;
  }
  Serial.print(mounted ? "MOUNT OK" : "MOUNT FAILED ");
  if (!mounted) Serial.print(mountResult);
  Serial.println();
  showMountState();
  return mounted;
}

/**
 * @brief Print why the nRF52 last reset (RESETREAS), then clear it.
 */
void printResetReason() {
  uint32_t reason = NRF_POWER->RESETREAS;
  NRF_POWER->RESETREAS = 0xFFFFFFFF; // write 1s to clear
  Serial.print("RESET REASON 0x");
  Serial.print(reason, HEX);
  if (reason == 0) Serial.print(" power-on");
  if (reason & POWER_RESETREAS_RESETPIN_Msk) Serial.print(" reset-pin");
  if (reason & POWER_RESETREAS_DOG_Msk) Serial.print(" watchdog");
  if (reason & POWER_RESETREAS_SREQ_Msk) Serial.print(" soft-reset");
  if (reason & POWER_RESETREAS_LOCKUP_Msk) Serial.print(" lockup");
  if (reason & POWER_RESETREAS_OFF_Msk) Serial.print(" wake-from-off");
  if (reason & POWER_RESETREAS_DIF_Msk) Serial.print(" debug-interface");
  Serial.println();
}

long getFileLen(FILE *f) {
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  return len;
}

/**
 * @brief Print a file's size and sha256.
 */
void printSha256(const char* name) {
  String path = String("/fs/") + name;
  FILE* f = fopen(path.c_str(), "rb");
  if (f == NULL) {
    Serial.println("    (unreadable)");
    return;
  }

  uint8_t packet[256];
  uint8_t output[32];
  Serial.print("    ");
  Serial.print(getFileLen(f));
  Serial.print("    ");

  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  while (!feof(f)) {
    int howMany = fread(packet, 1, sizeof(packet), f);
    mbedtls_sha256_update(&ctx, packet, howMany);
  }
  mbedtls_sha256_finish(&ctx, output);
  fclose(f);

  for (int i = 0; i < 32; i++) {
    if (output[i] < 0x10) Serial.print("0");
    Serial.print(output[i], HEX);
  }
  Serial.println();
}

/**
 * @brief List the files on the flash, optionally with size and sha256, then "END".
 */
void listFiles(bool withSha) {
  DIR* dir = mounted ? opendir("/fs") : NULL;
  if (dir == NULL) {
    Serial.println("NOT MOUNTED");
  } else {
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
      Serial.println(ent->d_name);
      if (withSha && ent->d_type == DT_REG) printSha256(ent->d_name);
    }
    closedir(dir);
  }
  Serial.println("END");
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("BOOT serial");
  printResetReason();

  // Keep the NDP120 off the shared SPI bus while we talk to the flash.
  pinMode(kNdpReset, OUTPUT);
  digitalWrite(kNdpReset, LOW);
  pinMode(kNdpCs, OUTPUT);
  digitalWrite(kNdpCs, HIGH);
  Serial.println("BOOT ndp held in reset");

  nicla::begin();
  Serial.println("BOOT pmic");
  nicla::leds.begin();
  nicla::leds.setColor(blue);
  Serial.println("BOOT led");

  initResult = spif.init();
  Serial.print("BOOT flash init=");
  Serial.println(initResult);

  Serial.println("NDP PROVISIONER READY (flash not mounted, send M or F)");
  nicla::leds.setColor(yellow); // ready, flash not mounted yet
}

void loop() {
  if (!Serial.available()) {
    delay(10);
    return;
  }
  uint8_t command = Serial.read();

  if (command == 'Y') {
    nicla::leds.setColor(blue);
    FILE* f = mounted ? fopen("/fs/temp.bin", "wb") : NULL;
    while (Serial.available()) Serial.read();
    Serial.print("Y");
    if (f != NULL) {
      memset(filename, 0, sizeof(filename));
      int ret = Ymodem_Receive(f, 1024 * 1024, filename);
      fclose(f);
      String name = String(filename);
      if (ret > 0 && name != "") {
        name = "/fs/" + name;
        remove(name.c_str());
        rename("/fs/temp.bin", name.c_str());
      }
    }
    showMountState();
  } else if (command == 'F') {
    nicla::leds.setColor(blue);
    Serial.println("FORMATTING");
    int err = fs.reformat(&spif);
    mounted = err == 0;
    mountResult = err;
    Serial.print(mounted ? "FORMAT OK" : "FORMAT FAILED ");
    if (!mounted) Serial.print(err);
    Serial.println();
    showMountState();
  } else if (command == 'L') {
    listFiles(false);
  } else if (command == 'S') {
    listFiles(true);
  } else if (command == 'I') {
    printStatus();
  } else if (command == 'M') {
    mountFlash();
  }
}
