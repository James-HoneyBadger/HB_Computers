// SPDX-License-Identifier: MIT

#include "kestrel/model1982/rom.hpp"

#include "kestrel/model1982/map.hpp"

#include <array>

#include "kestrel/basic_rom.hpp"
#include "kestrel/monitor_rom.hpp"

namespace kestrel::model1982 {

namespace {

template <std::size_t N>
std::vector<std::uint8_t> image(const std::array<std::uint8_t, N>& rom) {
    return {rom.begin(), rom.end()};
}

}  // namespace

const std::vector<RomInfo>& romLibrary() {
    // Built once: the images are the ROMs this build assembled.
    static const std::vector<std::uint8_t> basic = image(rom::kBASICImage);
    static const std::vector<std::uint8_t> monitor = image(rom::kMONITORImage);
    static const std::vector<RomInfo> roms = {
        {"basic", "BASIC V2.0",
         "Floating point, expressions, variables, PRINT and LET.", map::kBasicRomBase,
         &basic},
        {"monitor", "MONITOR V1.0",
         "Machine code: examine, deposit, go, registers, tape save and load.",
         map::kBasicRomBase, &monitor},
    };
    return roms;
}

const RomInfo* findRom(std::string_view id) {
    for (const RomInfo& rom : romLibrary()) {
        if (rom.id == id) {
            return &rom;
        }
    }
    return nullptr;
}

}  // namespace kestrel::model1982
