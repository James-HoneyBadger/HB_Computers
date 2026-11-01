// SPDX-License-Identifier: MIT
//
// The HB-1's cassette: a byte stream with a motor.
//
// A 1982 machine's tape was the cheapest storage there was, and its interface
// was one bit: you turned the motor on and the ROM toggled a line, or you read
// the line back and timed the pulses. This device is the part under that: it
// hands out and accepts bytes in order, and it reports whether it is running.
// The pulse encoding, the leader tone and the checksum are the ROM's business.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace timewarp::hb1 {

class Hb1Tape {
public:
    enum class Mode { Stopped, Playing, Recording };

    void insert() noexcept { present_ = true; }
    void eject() noexcept {
        present_ = false;
        mode_ = Mode::Stopped;
    }
    [[nodiscard]] bool present() const noexcept { return present_; }

    /// A tape image from the host: the bytes, in order.
    void load(const std::vector<std::uint8_t>& bytes);
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
        return bytes_;
    }

    void play() noexcept { mode_ = Mode::Playing; }
    void record() noexcept { mode_ = Mode::Recording; }
    void stop() noexcept { mode_ = Mode::Stopped; }
    void rewind() noexcept { position_ = 0; }
    [[nodiscard]] Mode mode() const noexcept { return mode_; }

    /// Recording appends at the current position; playing reads from it.
    void writeByte(std::uint8_t value);
    [[nodiscard]] bool canRead() const noexcept {
        return mode_ == Mode::Playing && position_ < bytes_.size();
    }
    std::uint8_t readByte();

    /// bit 7 present, bit 6 playing, bit 5 recording, bit 4 more to read.
    [[nodiscard]] std::uint8_t status() const noexcept;

    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    void setPosition(std::size_t position) noexcept;

private:
    std::vector<std::uint8_t> bytes_;
    std::size_t position_ = 0;
    Mode mode_ = Mode::Stopped;
    bool present_ = false;
};

}  // namespace timewarp::hb1
