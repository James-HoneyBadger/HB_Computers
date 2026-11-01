// SPDX-License-Identifier: MIT

#include "timewarp/hb1/tape.hpp"

#include <algorithm>

namespace timewarp::hb1 {

void Hb1Tape::load(const std::vector<std::uint8_t>& bytes) {
    bytes_ = bytes;
    position_ = 0;
    present_ = true;
}

void Hb1Tape::writeByte(std::uint8_t value) {
    if (mode_ != Mode::Recording) {
        return;
    }
    if (position_ < bytes_.size()) {
        bytes_[position_] = value;
    } else {
        bytes_.push_back(value);
    }
    ++position_;
}

std::uint8_t Hb1Tape::readByte() {
    if (!canRead()) {
        return 0;
    }
    return bytes_[position_++];
}

std::uint8_t Hb1Tape::status() const noexcept {
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

void Hb1Tape::setPosition(std::size_t position) noexcept {
    position_ = std::min(position, bytes_.size());
}

}  // namespace timewarp::hb1
