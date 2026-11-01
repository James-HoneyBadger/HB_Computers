// SPDX-License-Identifier: MIT
//
// The HB-2's tube: 80 x 50 cells, a character each, plus an attribute byte per
// cell holding an ink and a paper colour.
//
// It is nothing but the two arrays the processor sees at $8000 and $8FA0. The
// scrolling, the cursor, the colours -- all of that is the ROM's work, done
// through these bytes, because on this machine there is no other way for
// anything to happen.

#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "timewarp/hb2/map.hpp"

namespace timewarp::hb2 {

class Hb2Video {
public:
    static constexpr int kColumns = map::kScreenColumns;
    static constexpr int kRows = map::kScreenRows;
    static constexpr int kCells = map::kScreenCells;

    Hb2Video() { powerOn(); }

    /// The screen the machine comes up with: spaces on black.
    void powerOn();

    [[nodiscard]] std::uint8_t cell(int index) const;
    void setCell(int index, std::uint8_t code);
    [[nodiscard]] std::uint8_t attribute(int index) const;
    void setAttribute(int index, std::uint8_t value);

    [[nodiscard]] std::uint8_t cellAt(int row, int column) const;

    /// One row as text, with trailing spaces removed. The cursor is not part of
    /// the text: it is the host's to draw.
    [[nodiscard]] std::string line(int row) const;
    /// All 50 rows, newline-separated, each trimmed.
    [[nodiscard]] std::string text() const;

private:
    std::array<std::uint8_t, kCells> cells_{};
    std::array<std::uint8_t, kCells> attributes_{};
};

}  // namespace timewarp::hb2
