// SPDX-License-Identifier: MIT
//
// The ROM library: what can be put in the socket.
//
// The HB-1 has one 16 KiB socket at $A000 and a 4 KiB cartridge slot at $9000.
// Everything that gives the machine a language or a tool is an image in one of
// those two places, and this is the table of the ones this build carries. A
// ROM is data: nothing in the emulator knows what any of them do.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace timewarp::hb1 {

struct RomInfo {
    /// The name the settings and the command line use: "basic", "monitor".
    std::string_view id;
    /// What the front panel says.
    std::string_view name;
    /// One line for a person choosing it.
    std::string_view description;
    /// Where the image loads. `map::kBasicRomBase` for a socket ROM,
    /// `map::kCartridgeBase` for a cartridge.
    std::uint16_t origin;
    /// The image itself: machine code the 6502 will fetch.
    const std::vector<std::uint8_t>* image;
};

/// Every ROM this build carries, socket ROMs and cartridges alike.
[[nodiscard]] const std::vector<RomInfo>& romLibrary();

/// The ROM with this id, or nullptr.
[[nodiscard]] const RomInfo* findRom(std::string_view id);

/// The ROM this build boots with when nothing else is chosen.
inline constexpr std::string_view kDefaultSocketRom = "basic";

}  // namespace timewarp::hb1
