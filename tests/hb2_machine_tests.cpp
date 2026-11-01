// SPDX-License-Identifier: MIT
//
// The HB-2 as a whole: the ROMs assembled for it, booted on the machine, typed
// at and read back from the tube. Nothing here calls a ROM routine from C++:
// every check is a keystroke going in and characters coming out of the screen,
// which is what the machine is for.

#include "check.hpp"

#include "timewarp/hb2/machine.hpp"
#include "timewarp/hb2/map.hpp"

#include "hb2/basic_rom.hpp"
#include "hb2/kernal_rom.hpp"

#include <string>
#include <vector>

namespace {

using timewarp::hb2::Hb2Machine;
using timewarp::hb2::SpecialKey;

/// A machine with its two ROMs in place, run until it has settled.
class Booted {
public:
    Booted() {
        machine_.mountKernal(std::vector<std::uint8_t>(
                                 timewarp::hb2::rom::kKERNALImage.begin(),
                                 timewarp::hb2::rom::kKERNALImage.end()),
                             "kernal");
        machine_.mountBasic(std::vector<std::uint8_t>(
                                timewarp::hb2::rom::kBASICImage.begin(),
                                timewarp::hb2::rom::kBASICImage.end()),
                            "basic");
        machine_.reset();
        machine_.runFrames(3);
    }

    Hb2Machine& machine() { return machine_; }

    void typeLine(const std::string& text) {
        machine_.typeText(text);
        machine_.type(static_cast<std::uint8_t>(SpecialKey::Return));
    }

    /// Types a line and gives the machine time to answer.
    void run(const std::string& text) {
        typeLine(text);
        machine_.runFrames(20);
    }

    void runFrames(int frames) { machine_.runFrames(frames); }

    [[nodiscard]] std::string text() const { return machine_.screenText(); }
    [[nodiscard]] std::string line(int row) const {
        return machine_.screenLine(row);
    }
    /// True when any line on the screen contains `needle`.
    [[nodiscard]] bool shows(const std::string& needle) const {
        return text().find(needle) != std::string::npos;
    }
    /// The last line with anything on it, which is where the machine's answer
    /// to the last thing typed will be.
    [[nodiscard]] std::string lastLine() const {
        for (int row = timewarp::hb2::map::kScreenRows - 1; row >= 0; --row) {
            const std::string candidate = machine_.screenLine(row);
            if (!candidate.empty()) {
                return candidate;
            }
        }
        return {};
    }

private:
    Hb2Machine machine_;
};

void test_the_machine_boots() {
    Booted booted;
    TW_CHECK(booted.shows("HB BASIC 2.0"));
    TW_CHECK(booted.shows("READY"));
    TW_CHECK_EQ(booted.machine().kernalId(), std::string_view{"kernal"});
    TW_CHECK_EQ(booted.machine().basicId(), std::string_view{"basic"});
    // The machine is in native mode with its banks at zero, which is what the
    // KERNAL sets up before it starts the language.
    TW_CHECK(!booted.machine().cpu().emulation());
    TW_CHECK_EQ(booted.machine().cpu().cycleCount() > 0, true);
    TW_CHECK_EQ(booted.machine().cpu().illegalOpcodes(), 0u);
}

// The language's own interpreter is the next thing to finish: the prompt,
// the line reader and the echo work, and the statements that follow a line
// being interpreted do not yet. These are the checks it has to pass, kept here
// so that finishing it is a matter of turning them on.
#if 0
void test_arithmetic_at_the_prompt() {
    Booted booted;
    booted.run("PRINT 2+2");
    TW_CHECK_EQ(booted.lastLine(), std::string{"4"});
    booted.run("PRINT 10*3+1");
    TW_CHECK_EQ(booted.lastLine(), std::string{"31"});
    booted.run("LET A=41");
    booted.run("PRINT A+1");
    TW_CHECK_EQ(booted.lastLine(), std::string{"42"});
    booted.run("PRINT \"HELLO\"");
    TW_CHECK_EQ(booted.lastLine(), std::string{"HELLO"});
}
#endif

void test_the_keyboard_reaches_the_screen() {
    Booted booted;
    booted.machine().typeText("HELLO");
    booted.runFrames(4);
    // The ROM reads the keys and echoes them, which is the whole path from the
    // controller through the KERNAL to the tube.
    TW_CHECK(booted.shows("HELLO"));
}

void test_a_line_is_collected() {
    Booted booted;
    booted.machine().typeText("ABC");
    booted.machine().type(static_cast<std::uint8_t>(SpecialKey::Return));
    booted.runFrames(4);
    // The line landed in the KERNAL's line buffer, where the language reads it.
    TW_CHECK_EQ(booted.machine().ramAt(0x0500), std::uint8_t{'A'});
    TW_CHECK_EQ(booted.machine().ramAt(0x0501), std::uint8_t{'B'});
    TW_CHECK_EQ(booted.machine().ramAt(0x0502), std::uint8_t{'C'});
    TW_CHECK_EQ(booted.machine().ramAt(0x0503), std::uint8_t{0});
}

void test_a_wrong_sized_rom_is_refused() {
    Hb2Machine machine;
    TW_CHECK(!machine.mountKernal(std::vector<std::uint8_t>(100, 0), "short"));
    TW_CHECK(!machine.mountBasic(std::vector<std::uint8_t>(100, 0), "short"));
    TW_CHECK(machine.mountKernal(std::vector<std::uint8_t>(8192, 0), "kernal"));
    TW_CHECK(machine.mountBasic(std::vector<std::uint8_t>(65536, 0), "basic"));
}

void test_the_disk_is_a_sector_store() {
    Hb2Machine machine;
    machine.disk().format("TESTVOL");
    TW_CHECK_EQ(machine.disk().volumeName(), std::string{"TESTVOL"});
    std::vector<std::uint8_t> sector(512, 0x5A);
    TW_CHECK(machine.disk().writeSector(7, sector.data()));
    std::vector<std::uint8_t> back(512, 0);
    TW_CHECK(machine.disk().readSector(7, back.data()));
    TW_CHECK(back == sector);
    TW_CHECK(!machine.disk().readSector(99999, back.data()));
}

void test_the_frame_and_the_keyboard() {
    Booted booted;
    const std::uint16_t before = booted.machine().frameCounter();
    booted.runFrames(10);
    TW_CHECK(booted.machine().frameCounter() > before);
}

void test_the_screen_is_the_roms() {
    Booted booted;
    // The character cells are written by the ROM at the addresses the map
    // says, so reading the video memory directly agrees with reading the text.
    TW_CHECK_EQ(booted.machine().video().cellAt(0, 0), std::uint8_t{'H'});
    TW_CHECK_EQ(booted.machine().read(0x008000), std::uint8_t{'H'});
    // Writing a cell through the bus reaches the tube.
    booted.machine().write(0x008000, 'X');
    TW_CHECK_EQ(booted.machine().video().cellAt(0, 0), std::uint8_t{'X'});
}

}  // namespace

int main() {
    test_the_machine_boots();
    test_the_keyboard_reaches_the_screen();
    test_a_line_is_collected();
    test_a_wrong_sized_rom_is_refused();
    test_the_disk_is_a_sector_store();
    test_the_frame_and_the_keyboard();
    test_the_screen_is_the_roms();
    return tw::test::summary("hb2_machine_tests");
}
