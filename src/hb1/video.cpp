// SPDX-License-Identifier: MIT

#include "timewarp/hb1/video.hpp"

namespace timewarp::hb1 {

void Hb1Video::powerOn() {
    cells_.fill(static_cast<std::uint8_t>(' '));
    attributes_.fill(static_cast<std::uint8_t>((map::kLightGrey << 4) | map::kBlack));
}

std::uint8_t Hb1Video::cell(int index) const {
    if (index < 0 || index >= kCells) {
        return static_cast<std::uint8_t>(' ');
    }
    return cells_[static_cast<std::size_t>(index)];
}

void Hb1Video::setCell(int index, std::uint8_t code) {
    if (index < 0 || index >= kCells) {
        return;
    }
    cells_[static_cast<std::size_t>(index)] = code;
}

std::uint8_t Hb1Video::attribute(int index) const {
    if (index < 0 || index >= kCells) {
        return static_cast<std::uint8_t>((map::kLightGrey << 4) | map::kBlack);
    }
    return attributes_[static_cast<std::size_t>(index)];
}

void Hb1Video::setAttribute(int index, std::uint8_t value) {
    if (index < 0 || index >= kCells) {
        return;
    }
    attributes_[static_cast<std::size_t>(index)] = value;
}

std::uint8_t Hb1Video::cellAt(int row, int column) const {
    return cell(row * kColumns + column);
}

void Hb1Video::setCellAt(int row, int column, std::uint8_t code) {
    setCell(row * kColumns + column, code);
}

std::string Hb1Video::line(int row) const {
    if (row < 0 || row >= kRows) {
        return {};
    }
    std::string text;
    text.reserve(kColumns);
    for (int column = 0; column < kColumns; ++column) {
        text.push_back(static_cast<char>(cellAt(row, column)));
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    while (!text.empty() && text.back() == '\0') {
        text.pop_back();
    }
    return text;
}

std::string Hb1Video::text() const {
    std::string text;
    text.reserve(static_cast<std::size_t>(kRows) * (kColumns + 1));
    for (int row = 0; row < kRows; ++row) {
        text += line(row);
        if (row + 1 < kRows) {
            text.push_back('\n');
        }
    }
    return text;
}

}  // namespace timewarp::hb1
