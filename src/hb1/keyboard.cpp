// SPDX-License-Identifier: MIT

#include "timewarp/hb1/keyboard.hpp"

namespace timewarp::hb1 {

bool Hb1Keyboard::push(std::uint8_t key) {
    lastKey_ = key;
    if (static_cast<int>(buffer_.size()) >= kBufferSize) {
        return false;
    }
    buffer_.push_back(key);
    return true;
}

std::uint8_t Hb1Keyboard::read() {
    if (buffer_.empty()) {
        return 0;
    }
    const std::uint8_t key = buffer_.front();
    buffer_.pop_front();
    return key;
}

}  // namespace timewarp::hb1
