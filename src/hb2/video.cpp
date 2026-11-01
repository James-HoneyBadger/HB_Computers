// SPDX-License-Identifier: MIT

#include "timewarp/hb2/video.hpp"

namespace timewarp::hb2 {

void Hb2Video::powerOn() {
    cells_.fill(' ');
    attributes_.fill(static_cast<std::uint8_t>(map::kLightGrey << 4));
}

std::uint8_t Hb2Video::cell(int index) const {
    if (index < 0 || index >= kCells) {
        return 0;
    }
    return cells_[static_cast<std::size_t>(index)];
}

void Hb2Video::setCell(int index, std::uint8_t code) {
    if (index < 0 || index >= kCells) {
        return;
    }
    cells_[static_cast<std::size_t>(index)] = code;
}

std::uint8_t Hb2Video::attribute(int index) const {
    if (index < 0 || index >= kCells) {
        return 0;
    }
    return attributes_[static_cast<std::size_t>(index)];
}

void Hb2Video::setAttribute(int index, std::uint8_t value) {
    if (index < 0 || index >= kCells) {
        return;
    }
    attributes_[static_cast<std::size_t>(index)] = value;
}

std::uint8_t Hb2Video::cellAt(int row, int column) const {
    if (row < 0 || row >= kRows || column < 0 || column >= kColumns) {
        return 0;
    }
    return cells_[static_cast<std::size_t>(row * kColumns + column)];
}

std::string Hb2Video::line(int row) const {
    if (row < 0 || row >= kRows) {
        return {};
    }
    std::string text;
    text.reserve(kColumns);
    for (int column = 0; column < kColumns; ++column) {
        const std::uint8_t code = cellAt(row, column);
        text.push_back(code >= 32 && code < 127 ? static_cast<char>(code) : ' ');
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

std::string Hb2Video::text() const {
    std::string all;
    for (int row = 0; row < kRows; ++row) {
        if (row != 0) {
            all.push_back('\n');
        }
        all += line(row);
    }
    return all;
}

}  // namespace timewarp::hb2
