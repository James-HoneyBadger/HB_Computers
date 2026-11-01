// SPDX-License-Identifier: MIT
//
// The HB-2's cassette: a byte stream with a play head, which is all a tape ever
// was to the machine. The KERNAL owns the block format.

#pragma once

#include <cstdint>
#include <vector>

namespace timewarp::hb2 {

class Hb2Tape {
public:
    enum class Mode { Stopped, Playing, Recording };

    void insert() noexcept { present_ = true; }
    void eject() noexcept {
        present_ = false;
        mode_ = Mode::Stopped;
    }
    [[nodiscard]] bool present() const noexcept { return present_; }

    /// Puts a recording on the tape and rewinds it.
    void load(const std::vector<std::uint8_t>& bytes);
    [[nodiscard]] const std::vector<std::uint8_t>& contents() const noexcept {
        return data_;
    }

    void play() noexcept { mode_ = Mode::Playing; }
    void record() noexcept { mode_ = Mode::Recording; }
    void stop() noexcept { mode_ = Mode::Stopped; }
    void rewind() noexcept { position_ = 0; }
    [[nodiscard]] Mode mode() const noexcept { return mode_; }

    /// Appends a byte while recording.
    void writeByte(std::uint8_t value);
    /// The next byte while playing, or zero at the end.
    [[nodiscard]] std::uint8_t readByte();
    [[nodiscard]] bool dataLeft() const noexcept {
        return mode_ == Mode::Playing && position_ < data_.size();
    }
    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    void setPosition(std::size_t position) noexcept { position_ = position; }

private:
    std::vector<std::uint8_t> data_;
    std::size_t position_ = 0;
    Mode mode_ = Mode::Stopped;
    bool present_ = false;
};

}  // namespace timewarp::hb2
