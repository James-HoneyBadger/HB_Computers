// SPDX-License-Identifier: MIT
//
// The HB-2's disk: a sector store, and nothing more.
//
// The filesystem is not here. HB DOS 2.0 is machine code in the KERNAL ROM and
// works entirely through this device's four commands, which is why a program
// that formats a disk, saves a file and catalogs it exercises the ROM rather
// than the emulator.
//
// A volume is 4096 sectors of 512 bytes: two megabytes, which is what a 3.5"
// floppy held in 1992.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "timewarp/hb2/map.hpp"

namespace timewarp::hb2 {

class Hb2Disk {
public:
    Hb2Disk();

    /// A drive with no disk in it answers every command with an error.
    void eject() noexcept;
    void insert();
    [[nodiscard]] bool present() const noexcept { return present_; }

    /// Puts a blank, formatted-by-nobody volume in the drive: every sector
    /// zero. A real disk is taken by `load`.
    void format(std::string_view volume = "HBWARP");

    [[nodiscard]] const std::vector<std::uint8_t>& image() const noexcept {
        return image_;
    }
    /// Makes a disk image the drive's disk. Wrong-sized images are refused.
    bool load(const std::vector<std::uint8_t>& bytes);
    /// Loads a volume, or formats a fresh one when the image is the wrong size
    /// or empty. This is what a machine does at power-on.
    void loadOrFormat(const std::vector<std::uint8_t>& bytes,
                      std::string_view volume = "HBWARP");

    [[nodiscard]] bool readSector(std::uint32_t sector,
                                  std::uint8_t* destination) const;
    bool writeSector(std::uint32_t sector, const std::uint8_t* source);

    [[nodiscard]] std::string volumeName() const;

private:
    static constexpr std::size_t kSize =
        static_cast<std::size_t>(map::kDiskSectors) * map::kDiskSectorSize;

    std::vector<std::uint8_t> image_;
    bool present_ = false;
};

}  // namespace timewarp::hb2
