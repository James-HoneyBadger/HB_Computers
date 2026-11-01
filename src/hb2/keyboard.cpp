// SPDX-License-Identifier: MIT

#include "timewarp/hb2/keyboard.hpp"

namespace timewarp::hb2 {

bool Hb2Keyboard::push(std::uint8_t key) {
    if (buffer_.size() >= kCapacity) {
        return false;
    }
    buffer_.push_back(key);
    return true;
}

std::uint8_t Hb2Keyboard::read() {
    if (buffer_.empty()) {
        return 0;
    }
    const std::uint8_t key = buffer_.front();
    buffer_.pop_front();
    return key;
}

}  // namespace timewarp::hb2
