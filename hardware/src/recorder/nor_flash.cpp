/**
 * @file nor_flash.cpp
 *
 * @brief Implementation of the minimal SPI NOR flash block device.
 *
 * @details
 * Every operation is one or more chip-select transactions under the SPI
 * bus lock (the bus is shared with the NDP120). Transfers are split into
 * pieces of at most @c kMaxTransferBytes because the nRF52832's SPI DMA
 * moves at most 255 bytes at a time.
 *
 */

#include "nor_flash.h"

namespace {
/** @brief Largest single SPI transfer (the nRF52832's DMA limit is 255 bytes). */
constexpr size_t kMaxTransferBytes = 128;

constexpr uint8_t kCmdRead = 0x03;
constexpr uint8_t kCmdPageProgram = 0x02;
constexpr uint8_t kCmdSectorErase = 0x20;
constexpr uint8_t kCmdWriteEnable = 0x06;
constexpr uint8_t kCmdReadStatus = 0x05;
constexpr uint8_t kCmdReadId = 0x9F;
constexpr uint8_t kCmdReleasePowerDown = 0xAB;
constexpr uint8_t kCmdResetEnable = 0x66;
constexpr uint8_t kCmdReset = 0x99;

/** @brief Status register write-in-progress bit. */
constexpr uint8_t kStatusBusy = 0x01;

/** @brief Longest a page program may take (datasheet maximum is a few ms). */
constexpr uint32_t kProgramTimeoutUs = 20UL * 1000UL;
/** @brief Longest a sector erase may take (datasheet maximum is a few hundred ms). */
constexpr uint32_t kEraseTimeoutUs = 1000UL * 1000UL;
} // namespace

NorFlash::NorFlash(PinName mosi, PinName miso, PinName sclk, PinName cs, int hz)
    : _spi(mosi, miso, sclk), _cs(cs, 1), _hz(hz) {}

void NorFlash::transfer(uint8_t command, const uint32_t* addr, const uint8_t* tx, size_t txBytes,
                        uint8_t* rx, size_t rxBytes) {
    char header[4] = {static_cast<char>(command)};
    int headerBytes = 1;
    if (addr != nullptr) {
        header[1] = static_cast<char>(*addr >> 16);
        header[2] = static_cast<char>(*addr >> 8);
        header[3] = static_cast<char>(*addr);
        headerBytes = 4;
    }

    _spi.lock();
    _spi.format(8, 0);
    _spi.frequency(_hz);
    _cs = 0;
    _spi.write(header, headerBytes, nullptr, 0);
    while (txBytes > 0) {
        size_t n = txBytes < kMaxTransferBytes ? txBytes : kMaxTransferBytes;
        _spi.write(reinterpret_cast<const char*>(tx), static_cast<int>(n), nullptr, 0);
        tx += n;
        txBytes -= n;
    }
    while (rxBytes > 0) {
        size_t n = rxBytes < kMaxTransferBytes ? rxBytes : kMaxTransferBytes;
        _spi.write(nullptr, 0, reinterpret_cast<char*>(rx), static_cast<int>(n));
        rx += n;
        rxBytes -= n;
    }
    _cs = 1;
    _spi.unlock();
}

bool NorFlash::waitReady(uint32_t timeoutUs) {
    uint32_t start = micros();
    while (true) {
        uint8_t status = 0;
        transfer(kCmdReadStatus, nullptr, nullptr, 0, &status, 1);
        if (!(status & kStatusBusy)) return true;
        if (micros() - start > timeoutUs) return false;
    }
}

void NorFlash::writeEnable() {
    transfer(kCmdWriteEnable, nullptr, nullptr, 0, nullptr, 0);
}

int NorFlash::init() {
    transfer(kCmdReleasePowerDown, nullptr, nullptr, 0, nullptr, 0);
    delayMicroseconds(50);
    transfer(kCmdResetEnable, nullptr, nullptr, 0, nullptr, 0);
    transfer(kCmdReset, nullptr, nullptr, 0, nullptr, 0);
    delayMicroseconds(100);

    // JEDEC ID: manufacturer, memory type, capacity (log2 of the size in bytes).
    uint8_t id[3] = {0, 0, 0};
    transfer(kCmdReadId, nullptr, nullptr, 0, id, sizeof(id));
    if (id[2] < 16 || id[2] > 24) return mbed::BD_ERROR_DEVICE_ERROR; // no chip, or > 16 MB (needs 4-byte addresses)
    _size = static_cast<mbed::bd_size_t>(1) << id[2];
    return waitReady(kProgramTimeoutUs) ? mbed::BD_ERROR_OK : mbed::BD_ERROR_DEVICE_ERROR;
}

int NorFlash::deinit() {
    return mbed::BD_ERROR_OK;
}

int NorFlash::read(void* buffer, mbed::bd_addr_t addr, mbed::bd_size_t size) {
    if (!is_valid_read(addr, size)) return mbed::BD_ERROR_DEVICE_ERROR;
    uint32_t address = static_cast<uint32_t>(addr);
    transfer(kCmdRead, &address, nullptr, 0, static_cast<uint8_t*>(buffer), static_cast<size_t>(size));
    return mbed::BD_ERROR_OK;
}

int NorFlash::program(const void* buffer, mbed::bd_addr_t addr, mbed::bd_size_t size) {
    if (!is_valid_program(addr, size)) return mbed::BD_ERROR_DEVICE_ERROR;
    const uint8_t* data = static_cast<const uint8_t*>(buffer);
    uint32_t address = static_cast<uint32_t>(addr);
    uint32_t remaining = static_cast<uint32_t>(size);
    while (remaining > 0) {
        uint32_t n = kPageBytes - (address % kPageBytes); // stop at the page boundary
        if (n > remaining) n = remaining;
        writeEnable();
        transfer(kCmdPageProgram, &address, data, n, nullptr, 0);
        if (!waitReady(kProgramTimeoutUs)) return mbed::BD_ERROR_DEVICE_ERROR;
        address += n;
        data += n;
        remaining -= n;
    }
    return mbed::BD_ERROR_OK;
}

int NorFlash::erase(mbed::bd_addr_t addr, mbed::bd_size_t size) {
    if (!is_valid_erase(addr, size)) return mbed::BD_ERROR_DEVICE_ERROR;
    uint32_t address = static_cast<uint32_t>(addr);
    uint32_t end = static_cast<uint32_t>(addr + size);
    for (; address < end; address += kSectorBytes) {
        writeEnable();
        transfer(kCmdSectorErase, &address, nullptr, 0, nullptr, 0);
        if (!waitReady(kEraseTimeoutUs)) return mbed::BD_ERROR_DEVICE_ERROR;
    }
    return mbed::BD_ERROR_OK;
}
