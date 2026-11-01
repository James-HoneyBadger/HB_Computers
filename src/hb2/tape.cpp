// SPDX-License-Identifier: MIT

#include "timewarp/hb2/tape.hpp"

namespace timewarp::hb2 {

void Hb2Tape::load(const std::vector<std::uint8_t>& bytes) {
    data_ = bytes;
    position_ = 0;
    present_ = true;
    mode_ = Mode::Stopped;
}

void Hb2Tape::writeByte(std::uint8_t value) {
    if (mode_ != Mode::Recording) {
        return;
    }
    if (position_ == data_.size()) {
        data_.push_back(value);
        ++position_;
        return;
    }
    data_[position_] = value;
    ++position_;
}

std::uint8_t Hb2Tape::readByte() {
    if (mode_ != Mode::Playing || position_ >= data_.size()) {
        return 0;
    }
    return data_[position_++];
}

}  // namespace timewarp::hb2
