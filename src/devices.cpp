// SPDX-License-Identifier: MIT

#include "timewarp/devices.hpp"

#include <algorithm>
#include <cstring>

namespace timewarp {

namespace {
constexpr std::size_t kVolumeNameOffset = 8;
constexpr std::size_t kVolumeNameLength = 16;
}  // namespace

// ---------------------------------------------------------------------------
// The tube
// ---------------------------------------------------------------------------

Video::Video(int columns, int rows)
    : columns_(columns),
      rows_(rows),
      cellCount_(columns * rows),
      cells_(static_cast<std::size_t>(cellCount_), ' '),
      attributes_(static_cast<std::size_t>(cellCount_), 0) {}

void Video::powerOn(std::uint8_t ink, std::uint8_t paper) {
    cells_.assign(static_cast<std::size_t>(cellCount_), ' ');
    attributes_.assign(static_cast<std::size_t>(cellCount_),
                       static_cast<std::uint8_t>((ink << 4) | paper));
}

std::uint8_t Video::cell(int index) const {
    if (index < 0 || index >= cellCount_) {
        return 0;
    }
    return cells_[static_cast<std::size_t>(index)];
}

void Video::setCell(int index, std::uint8_t code) {
    if (index < 0 || index >= cellCount_) {
        return;
    }
    cells_[static_cast<std::size_t>(index)] = code;
}

std::uint8_t Video::attribute(int index) const {
    if (index < 0 || index >= cellCount_) {
        return 0;
    }
    return attributes_[static_cast<std::size_t>(index)];
}

void Video::setAttribute(int index, std::uint8_t value) {
    if (index < 0 || index >= cellCount_) {
        return;
    }
    attributes_[static_cast<std::size_t>(index)] = value;
}

std::uint8_t Video::cellAt(int row, int column) const {
    if (row < 0 || row >= rows_ || column < 0 || column >= columns_) {
        return 0;
    }
    return cells_[static_cast<std::size_t>(row * columns_ + column)];
}

std::string Video::line(int row) const {
    if (row < 0 || row >= rows_) {
        return {};
    }
    std::string text;
    text.reserve(static_cast<std::size_t>(columns_));
    for (int column = 0; column < columns_; ++column) {
        const std::uint8_t code = cellAt(row, column);
        text.push_back(code >= 32 && code < 127 ? static_cast<char>(code) : ' ');
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

std::string Video::text() const {
    std::string all;
    for (int row = 0; row < rows_; ++row) {
        if (row != 0) {
            all.push_back('\n');
        }
        all += line(row);
    }
    return all;
}

// ---------------------------------------------------------------------------
// The keyboard
// ---------------------------------------------------------------------------

Keyboard::Keyboard(int capacity) : capacity_(capacity) {}

bool Keyboard::push(std::uint8_t key) {
    if (static_cast<int>(buffer_.size()) >= capacity_) {
        return false;
    }
    lastKey_ = key;
    buffer_.push_back(key);
    return true;
}

std::uint8_t Keyboard::read() {
    if (buffer_.empty()) {
        return 0;
    }
    const std::uint8_t key = buffer_.front();
    buffer_.pop_front();
    return key;
}

// ---------------------------------------------------------------------------
// The disk
// ---------------------------------------------------------------------------

Disk::Disk(int sectors, int sectorSize)
    : sectors_(sectors),
      sectorSize_(sectorSize),
      bytes_(static_cast<std::size_t>(sectors) * static_cast<std::size_t>(sectorSize), 0) {
    format();
}

void Disk::insert() {
    if (static_cast<int>(bytes_.size()) != sectors_ * sectorSize_) {
        bytes_.assign(static_cast<std::size_t>(sectors_) * static_cast<std::size_t>(sectorSize_), 0);
    }
    present_ = true;
}

void Disk::eject() noexcept {
    present_ = false;
}

void Disk::format(std::string_view volume) {
    bytes_.assign(static_cast<std::size_t>(sectors_) * static_cast<std::size_t>(sectorSize_), 0);
    const std::string name{volume};
    const std::size_t length = std::min(name.size(), kVolumeNameLength);
    std::memcpy(bytes_.data() + kVolumeNameOffset, name.data(), length);
    present_ = true;
}

std::string Disk::volumeName() const {
    std::string name;
    if (bytes_.size() < kVolumeNameOffset + kVolumeNameLength) {
        return name;
    }
    for (std::size_t i = 0; i < kVolumeNameLength; ++i) {
        const char c = static_cast<char>(bytes_[kVolumeNameOffset + i]);
        if (c == '\0') {
            break;
        }
        name.push_back(c);
    }
    return name;
}

bool Disk::load(const std::vector<std::uint8_t>& bytes) {
    if (static_cast<int>(bytes.size()) != sectors_ * sectorSize_) {
        return false;
    }
    bytes_ = bytes;
    present_ = true;
    return true;
}

void Disk::loadOrFormat(const std::vector<std::uint8_t>& bytes,
                        std::string_view volume) {
    if (!bytes.empty() && load(bytes)) {
        return;
    }
    format(volume);
}

std::uint8_t* Disk::sector(int index) {
    if (!present_ || index < 0 || index >= sectors_) {
        return nullptr;
    }
    return bytes_.data() + static_cast<std::size_t>(index) * static_cast<std::size_t>(sectorSize_);
}

const std::uint8_t* Disk::sector(int index) const {
    if (!present_ || index < 0 || index >= sectors_) {
        return nullptr;
    }
    return bytes_.data() + static_cast<std::size_t>(index) * static_cast<std::size_t>(sectorSize_);
}

bool Disk::readSector(std::uint32_t index, std::uint8_t* into) const {
    const std::uint8_t* source = sector(static_cast<int>(index));
    if (source == nullptr || into == nullptr) {
        return false;
    }
    std::memcpy(into, source, static_cast<std::size_t>(sectorSize_));
    return true;
}

bool Disk::writeSector(std::uint32_t index, const std::uint8_t* from) {
    std::uint8_t* target = sector(static_cast<int>(index));
    if (target == nullptr || from == nullptr) {
        return false;
    }
    std::memcpy(target, from, static_cast<std::size_t>(sectorSize_));
    return true;
}

// ---------------------------------------------------------------------------
// The tape
// ---------------------------------------------------------------------------

void Tape::load(const std::vector<std::uint8_t>& bytes) {
    bytes_ = bytes;
    position_ = 0;
    present_ = true;
    mode_ = Mode::Stopped;
}

void Tape::writeByte(std::uint8_t value) {
    if (mode_ != Mode::Recording) {
        return;
    }
    if (position_ >= bytes_.size()) {
        bytes_.push_back(value);
    } else {
        bytes_[position_] = value;
    }
    ++position_;
}

std::uint8_t Tape::readByte() {
    if (!canRead()) {
        return 0;
    }
    return bytes_[position_++];
}

std::uint8_t Tape::status() const noexcept {
    std::uint8_t status = 0;
    if (present_) {
        status |= 0x80;
    }
    if (mode_ == Mode::Playing) {
        status |= 0x40;
    }
    if (mode_ == Mode::Recording) {
        status |= 0x20;
    }
    if (canRead()) {
        status |= 0x10;
    }
    return status;
}

void Tape::setPosition(std::size_t position) noexcept {
    position_ = std::min(position, bytes_.size());
}

}  // namespace timewarp
