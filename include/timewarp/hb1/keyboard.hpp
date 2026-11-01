// SPDX-License-Identifier: MIT
//
// The HB-1's keyboard: a queue of ASCII keys waiting to be read.
//
// The device holds a small buffer and no more -- the mapping from key to
// character is the ROM's, and so is the decision of when a key means anything.
// The queue is what a machine with a keyboard controller really has: the host
// types into it, the ROM reads out of it.

#pragma once

#include <cstdint>
#include <deque>

namespace timewarp::hb1 {

/// The keys that are not characters. They are byte codes the ROM defines, in
/// the range a keyboard controller would report for keys that never produce a
/// printable character.
enum class SpecialKey : std::uint8_t {
    Return = 0x0D,
    Backspace = 0x08,
    Escape = 0x1B,
    Left = 0x1C,
    Right = 0x1D,
    Up = 0x1E,
    Down = 0x1F,
    Delete = 0x7F,
};

class Hb1Keyboard {
public:
    /// How many keys the controller can hold before the host has to wait.
    static constexpr int kBufferSize = 16;

    /// Queues a key. Returns false when the buffer is full, which is what a
    /// controller with nowhere to put a byte would report.
    bool push(std::uint8_t key);
    [[nodiscard]] bool available() const noexcept { return !buffer_.empty(); }
    [[nodiscard]] int size() const noexcept {
        return static_cast<int>(buffer_.size());
    }
    /// Takes the oldest key, or 0 when there is none.
    std::uint8_t read();
    void clear() noexcept { buffer_.clear(); }

    /// The last key the controller saw, whether or not it has been read.
    [[nodiscard]] std::uint8_t lastKey() const noexcept { return lastKey_; }

private:
    std::deque<std::uint8_t> buffer_;
    std::uint8_t lastKey_ = 0;
};

}  // namespace timewarp::hb1
