// SPDX-License-Identifier: MIT
//
// The HB-1's tube: 40 x 25 cells, a byte each, plus a colour byte per cell.
//
// It is nothing but the two arrays the processor sees at $8000 and $8400. The
// scrolling, the cursor, the colour of a character -- all of that is the ROM's
// work, done through these bytes, because on this machine there is no other way
// for anything to happen. A host that painted its own screen would be the thing
// this machine exists not to be.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "timewarp/hb1/map.hpp"

namespace timewarp::hb1 {

class Hb1Video {
public:
    static constexpr int kColumns = map::kScreenColumns;
    static constexpr int kRows = map::kScreenRows;
    static constexpr int kCells = map::kScreenCells;

    Hb1Video() { powerOn(); }

    /// The screen the machine comes up with: spaces on black.
    void powerOn();

    [[nodiscard]] std::uint8_t cell(int index) const;
    void setCell(int index, std::uint8_t code);
    [[nodiscard]] std::uint8_t attribute(int index) const;
    void setAttribute(int index, std::uint8_t value);

    [[nodiscard]] std::uint8_t cellAt(int row, int column) const;
    void setCellAt(int row, int column, std::uint8_t code);

    /// One row as text, with trailing spaces removed. The cursor is not part of
    /// the text: it is the host's to draw, and a test that compares text is
    /// comparing what the ROM wrote.
    [[nodiscard]] std::string line(int row) const;
    /// All 25 rows, newline-separated, each trimmed. This is the screen as a
    /// person reads it.
    [[nodiscard]] std::string text() const;

private:
    std::array<std::uint8_t, kCells> cells_{};
    std::array<std::uint8_t, kCells> attributes_{};
};

}  // namespace timewarp::hb1
