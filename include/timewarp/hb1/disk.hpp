// SPDX-License-Identifier: MIT
//
// The HB-1's disk: 512 sectors of 256 bytes, addressed by number.
//
// The controller knows nothing about files. It reads a sector into RAM and
// writes RAM onto a sector, and that is all a 1982 disk controller did -- the
// directory, the file names and the sector chains are HB DOS's, written in ROM
// on top of these two commands. A machine whose disk knew about files would have
// put the filesystem in the host, and the ROM would be a costume again.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace timewarp::hb1 {

class Hb1Disk {
public:
    static constexpr int kSectors = 512;
    static constexpr int kSectorSize = 256;
    static constexpr int kByteSize = kSectors * kSectorSize;

    /// A blank volume: a volume header in sector 0 and nothing else. The disk is
    /// present from the moment the machine is switched on, because a machine
    /// that shipped with a drive shipped with a formatted disk.
    Hb1Disk();

    [[nodiscard]] bool present() const noexcept { return present_; }
    void eject() noexcept;
    void insert();

    /// Writes the volume header: the signature, the volume name and the sector
    /// count. Every other sector becomes zero.
    void format(std::string_view volume = "HBWARP");

    [[nodiscard]] std::string volumeName() const;

    /// The whole volume, for the host to keep on a real disk.
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
        return bytes_;
    }
    /// Mounts a volume from the host. A volume of the wrong size is refused:
    /// half a disk is a worse lie than no disk.
    bool load(const std::vector<std::uint8_t>& bytes);
    void loadOrFormat(const std::vector<std::uint8_t>& bytes,
                      std::string_view volume = "HBWARP");

    [[nodiscard]] std::uint8_t* sector(int index);
    [[nodiscard]] const std::uint8_t* sector(int index) const;

private:
    std::vector<std::uint8_t> bytes_;
    bool present_ = true;
};

}  // namespace timewarp::hb1
