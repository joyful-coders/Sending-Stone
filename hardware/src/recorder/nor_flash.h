/**
 * @file nor_flash.h
 *
 * @brief Minimal block device for the Nicla Voice's external SPI NOR flash.
 *
 * @details
 * A drop-in replacement for the core's @c SPIFBlockDevice, used by the
 * recorder. The core's driver sleeps 1 ms (an RTOS sleep, which takes several
 * ms on this board) before every busy check, and checks at least twice per
 * 256-byte page, so writing audio cost ~15 ms per page against the chip's
 * ~1 ms. This driver polls the chip's busy flag without sleeping.
 *
 * It reports the same geometry as @c SPIFBlockDevice (1-byte reads and
 * programs, 4 KB erase sectors, the chip's full size), so it mounts the same
 * LittleFS filesystem the NDP library's firmware files live on.
 *
 */

#pragma once

#include <Arduino.h>
#include <drivers/SPIMaster.h>
#include <drivers/DigitalOut.h>
#include <blockdevice/BlockDevice.h>

/** @brief SPI NOR flash with standard commands (read 0x03, page program 0x02, 4 KB sector erase 0x20). */
class NorFlash : public mbed::BlockDevice {
public:
    /**
     * @brief Set up the driver. Nothing touches the bus until @c init().
     *
     * @param mosi SPI MOSI pin.
     * @param miso SPI MISO pin.
     * @param sclk SPI clock pin.
     * @param cs Flash chip-select pin.
     * @param hz SPI clock frequency.
     *
     */
    NorFlash(PinName mosi, PinName miso, PinName sclk, PinName cs, int hz);

    /** @brief Wake and reset the chip, then read its size from the JEDEC ID. */
    int init() override;
    /** @brief Nothing to release. */
    int deinit() override;
    /** @brief Read @p size bytes at @p addr. */
    int read(void* buffer, mbed::bd_addr_t addr, mbed::bd_size_t size) override;
    /** @brief Program @p size bytes at @p addr (already erased), page by page. */
    int program(const void* buffer, mbed::bd_addr_t addr, mbed::bd_size_t size) override;
    /** @brief Erase whole 4 KB sectors covering @p addr to @p addr + @p size. */
    int erase(mbed::bd_addr_t addr, mbed::bd_size_t size) override;

    mbed::bd_size_t get_read_size() const override { return 1; }
    mbed::bd_size_t get_program_size() const override { return 1; }
    mbed::bd_size_t get_erase_size() const override { return kSectorBytes; }
    mbed::bd_size_t get_erase_size(mbed::bd_addr_t) const override { return kSectorBytes; }
    int get_erase_value() const override { return 0xFF; }
    mbed::bd_size_t size() const override { return _size; }
    const char* get_type() const override { return "NORFLASH"; }

private:
    /** @brief Erase sector size, in bytes. */
    static constexpr uint32_t kSectorBytes = 4096;
    /** @brief Program page size, in bytes. A program must not cross a page boundary. */
    static constexpr uint32_t kPageBytes = 256;

    /** @brief Send a command and optional address, then write @p tx and/or read @p rx, in one chip select. */
    void transfer(uint8_t command, const uint32_t* addr, const uint8_t* tx, size_t txBytes, uint8_t* rx, size_t rxBytes);
    /** @brief Wait until the chip's write-in-progress flag clears, up to @p timeoutUs. */
    bool waitReady(uint32_t timeoutUs);
    /** @brief Set the write-enable latch (required before every program and erase). */
    void writeEnable();

    mbed::SPI _spi;
    mbed::DigitalOut _cs;
    int _hz;
    mbed::bd_size_t _size = 0;
};
