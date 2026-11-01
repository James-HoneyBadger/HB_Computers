// SPDX-License-Identifier: MIT
//
// The HB-2's keyboard controller: a queue of bytes the ROM reads one at a time.
//
// The machine puts a byte here and the ROM is interrupted to come and collect
// it, the same way the HB-1's did. Nothing about the key's meaning lives on
// this side of the wire.

#pragma once

#include <cstdint>
#include <deque>

namespace timewarp::hb2 {

class Hb2Keyboard {
public:
    /// Queues a keystroke. Returns false if the queue is full, which is what a
    /// person typing faster than the machine would discover.
    bool push(std::uint8_t key);

    [[nodiscard]] std::uint8_t read();
    [[nodiscard]] bool available() const noexcept { return !buffer_.empty(); }
    void clear() noexcept { buffer_.clear(); }

private:
    static constexpr std::size_t kCapacity = 32;
    std::deque<std::uint8_t> buffer_;
};

}  // namespace timewarp::hb2
