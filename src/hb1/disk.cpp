// SPDX-License-Identifier: MIT

#include "timewarp/hb1/disk.hpp"

#include <algorithm>
#include <cstring>

namespace timewarp::hb1 {

namespace {

// Sector 0 is the volume header: a signature, the volume name, and the count.
constexpr int kSignatureOffset = 0;
constexpr int kVolumeNameOffset = 8;
constexpr int kVolumeNameLength = 16;
constexpr char kSignature[8] = {'H', 'B', 'D', 'O', 'S', '1', '0', 0};

}  // namespace

Hb1Disk::Hb1Disk() : bytes_(static_cast<std::size_t>(kByteSize), 0) {
    format();
}

void Hb1Disk::eject() noexcept {
    present_ = false;
}

void Hb1Disk::insert() {
    present_ = true;
}

void Hb1Disk::format(std::string_view volume) {
    std::fill(bytes_.begin(), bytes_.end(), 0);
    std::memcpy(bytes_.data() + kSignatureOffset, kSignature, sizeof kSignature);
    std::string name{volume};
    if (name.size() > static_cast<std::size_t>(kVolumeNameLength)) {
        name.resize(kVolumeNameLength);
    }
    name.resize(kVolumeNameLength, ' ');
    std::memcpy(bytes_.data() + kVolumeNameOffset, name.data(), kVolumeNameLength);
    bytes_[kVolumeNameOffset + kVolumeNameLength] = kSectors & 0xFF;
    bytes_[kVolumeNameOffset + kVolumeNameLength + 1] = (kSectors >> 8) & 0xFF;
    present_ = true;
}

std::string Hb1Disk::volumeName() const {
    std::string name{reinterpret_cast<const char*>(bytes_.data() + kVolumeNameOffset),
                     kVolumeNameLength};
    while (!name.empty() && (name.back() == ' ' || name.back() == '\0')) {
        name.pop_back();
    }
    return name;
}

bool Hb1Disk::load(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() != static_cast<std::size_t>(kByteSize)) {
        return false;
    }
    bytes_ = bytes;
    present_ = true;
    return true;
}

void Hb1Disk::loadOrFormat(const std::vector<std::uint8_t>& bytes,
                           std::string_view volume) {
    if (!load(bytes)) {
        format(volume);
    }
}

std::uint8_t* Hb1Disk::sector(int index) {
    if (index < 0 || index >= kSectors) {
        return nullptr;
    }
    return bytes_.data() + static_cast<std::size_t>(index) * kSectorSize;
}

const std::uint8_t* Hb1Disk::sector(int index) const {
    if (index < 0 || index >= kSectors) {
        return nullptr;
    }
    return bytes_.data() + static_cast<std::size_t>(index) * kSectorSize;
}

}  // namespace timewarp::hb1
