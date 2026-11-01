// SPDX-License-Identifier: MIT

#include "timewarp/hb2/disk.hpp"

#include <algorithm>
#include <cstring>

namespace timewarp::hb2 {

namespace {
constexpr std::size_t kVolumeNameOffset = 8;
constexpr std::size_t kVolumeNameLength = 16;
}  // namespace

Hb2Disk::Hb2Disk() { format(); }

void Hb2Disk::eject() noexcept {
    present_ = false;
}

void Hb2Disk::insert() {
    if (image_.size() != kSize) {
        image_.assign(kSize, 0);
    }
    present_ = true;
}

void Hb2Disk::format(std::string_view volume) {
    image_.assign(kSize, 0);
    constexpr std::string_view kSignature = "HBDOS20";
    std::memcpy(image_.data(), kSignature.data(), kSignature.size());
    const std::string name{volume};
    const std::size_t length = std::min(name.size(), kVolumeNameLength);
    std::memcpy(image_.data() + kVolumeNameOffset, name.data(), length);
    // Sector count, little-endian 32-bit, so a reader can tell how big a volume
    // it has been handed.
    image_[24] = static_cast<std::uint8_t>(map::kDiskSectors & 0xFF);
    image_[25] = static_cast<std::uint8_t>((map::kDiskSectors >> 8) & 0xFF);
    image_[26] = static_cast<std::uint8_t>((map::kDiskSectors >> 16) & 0xFF);
    image_[27] = static_cast<std::uint8_t>((map::kDiskSectors >> 24) & 0xFF);
    present_ = true;
}

bool Hb2Disk::load(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() != kSize) {
        return false;
    }
    image_ = bytes;
    present_ = true;
    return true;
}

void Hb2Disk::loadOrFormat(const std::vector<std::uint8_t>& bytes,
                           std::string_view volume) {
    if (!bytes.empty() && load(bytes)) {
        return;
    }
    format(volume);
}

bool Hb2Disk::readSector(std::uint32_t sector,
                         std::uint8_t* destination) const {
    if (!present_ || sector >= map::kDiskSectors || destination == nullptr) {
        return false;
    }
    const auto offset = static_cast<std::size_t>(sector) * map::kDiskSectorSize;
    std::memcpy(destination, image_.data() + offset, map::kDiskSectorSize);
    return true;
}

bool Hb2Disk::writeSector(std::uint32_t sector, const std::uint8_t* source) {
    if (!present_ || sector >= map::kDiskSectors || source == nullptr) {
        return false;
    }
    const auto offset = static_cast<std::size_t>(sector) * map::kDiskSectorSize;
    std::memcpy(image_.data() + offset, source, map::kDiskSectorSize);
    return true;
}

std::string Hb2Disk::volumeName() const {
    if (!present_ || image_.size() < kSize) {
        return {};
    }
    std::string name;
    for (std::size_t i = 0; i < kVolumeNameLength; ++i) {
        const char c = static_cast<char>(image_[kVolumeNameOffset + i]);
        if (c == '\0') {
            break;
        }
        name.push_back(c);
    }
    return name;
}

}  // namespace timewarp::hb2
