// SPDX-License-Identifier: MIT
//
// The Kestrel 16 as a whole: the ROMs assembled for it, booted on the machine, typed
// at and read back from the tube. Nothing here calls a ROM routine from C++:
// every check is a keystroke going in and characters coming out of the screen,
// which is what the machine is for.

#include "check.hpp"

#include "timewarp/machine.hpp"
#include "timewarp/kestrel16/map.hpp"

#include "kestrel16/basic_rom.hpp"
#include "kestrel16/kernal_rom.hpp"

#include <string>
#include <vector>

namespace {

using timewarp::Machine;
using timewarp::Model;
using timewarp::SpecialKey;

/// A machine with its two ROMs in place, run until it has settled.
class Booted {
public:
    Booted() {
        machine_.mountKernal(std::vector<std::uint8_t>(
                                 timewarp::kestrel16::rom::kKERNALImage.begin(),
                                 timewarp::kestrel16::rom::kKERNALImage.end()),
                             "kernal");
        machine_.mountBasic(std::vector<std::uint8_t>(
                                timewarp::kestrel16::rom::kBASICImage.begin(),
                                timewarp::kestrel16::rom::kBASICImage.end()),
                            "basic");
        machine_.reset();
        machine_.runFrames(3);
    }

    Machine& machine() { return machine_; }

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
    /// The last line with anything on it.
    [[nodiscard]] std::string lastLine() const {
        for (int row = timewarp::kestrel16::map::kScreenRows - 1; row >= 0; --row) {
            const std::string candidate = machine_.screenLine(row);
            if (!candidate.empty()) {
                return candidate;
            }
        }
        return {};
    }

    /// What the language printed in answer to the last line typed: everything
    /// above the prompt that follows it.
    [[nodiscard]] std::string answer() const {
        for (int row = timewarp::kestrel16::map::kScreenRows - 1; row >= 0; --row) {
            const std::string candidate = machine_.screenLine(row);
            if (candidate.empty()) {
                continue;
            }
            if (candidate.rfind("READY>", 0) == 0 || candidate == "?WHAT?") {
                continue;
            }
            return candidate;
        }
        return {};
    }

private:
    Machine machine_{Model::Kestrel16};
};

void test_one_machine_two_models() {
    // The point of the merge: one class, built for a model, with the profile
    // saying what differs.
    const Machine kestrel{Model::Kestrel};
    const Machine kestrel16{Model::Kestrel16};
    TW_CHECK_EQ(std::string{kestrel.modelName()}, std::string{"Kestrel"});
    TW_CHECK_EQ(std::string{kestrel16.modelName()}, std::string{"Kestrel 16"});
    TW_CHECK_EQ(kestrel.profile().clockHz, 1'000'000);
    TW_CHECK_EQ(kestrel16.profile().clockHz, 8'000'000);
    TW_CHECK_EQ(kestrel.profile().cyclesPerFrame, 20'000);
    TW_CHECK_EQ(kestrel16.profile().cyclesPerFrame, 160'000);
    TW_CHECK_EQ(kestrel.profile().columns, 40);
    TW_CHECK_EQ(kestrel16.profile().columns, 80);
    TW_CHECK_EQ(kestrel.profile().diskSectors, 512);
    TW_CHECK_EQ(kestrel16.profile().diskSectors, 4096);
    TW_CHECK_EQ(kestrel.profile().diskSectorSize, 256);
    TW_CHECK_EQ(kestrel16.profile().diskSectorSize, 512);
    // And each has the processor its machine really had.
    TW_CHECK_EQ(kestrel.cpu6502().cycleCount(), 0u);
    TW_CHECK_EQ(kestrel16.cpu65816().cycleCount(), 0u);
}

void test_the_machine_boots() {
    Booted booted;
    TW_CHECK(booted.shows("KESTREL 16 BASIC 2.0"));
    TW_CHECK(booted.shows("READY"));
    TW_CHECK_EQ(booted.machine().kernalId(), std::string_view{"kernal"});
    TW_CHECK_EQ(booted.machine().basicId(), std::string_view{"basic"});
    // The machine is in native mode with its banks at zero, which is what the
    // KERNAL sets up before it starts the language.
    TW_CHECK(!booted.machine().cpu65816().emulation());
    TW_CHECK_EQ(booted.machine().cpu65816().cycleCount() > 0, true);
    TW_CHECK_EQ(booted.machine().cpu65816().illegalOpcodes(), 0u);
}

void test_arithmetic_at_the_prompt() {
    Booted booted;
    booted.run("PRINT 2+2");
    TW_CHECK_EQ(booted.answer(), std::string{"4"});
    booted.run("PRINT 10*3+1");
    TW_CHECK_EQ(booted.answer(), std::string{"31"});
    booted.run("PRINT 100/4");
    TW_CHECK_EQ(booted.answer(), std::string{"25"});
    booted.run("PRINT 9-4");
    TW_CHECK_EQ(booted.answer(), std::string{"5"});
    booted.run("PRINT (2+3)*4");
    TW_CHECK_EQ(booted.answer(), std::string{"20"});
    booted.run("PRINT -5");
    TW_CHECK_EQ(booted.answer(), std::string{"-5"});
}

void test_variables_and_strings() {
    Booted booted;
    booted.run("LET A=41");
    booted.run("PRINT A+1");
    TW_CHECK_EQ(booted.answer(), std::string{"42"});
    booted.run("PRINT \"HELLO\"");
    TW_CHECK_EQ(booted.answer(), std::string{"HELLO"});
    booted.run("PRINT \"A=\";A");
    TW_CHECK_EQ(booted.answer(), std::string{"A=41"});
}

void test_sixteen_bit_numbers() {
    Booted booted;
    booted.run("PRINT 30000+1");
    TW_CHECK_EQ(booted.answer(), std::string{"30001"});
    booted.run("LET B=1000");
    booted.run("PRINT B*20");
    TW_CHECK_EQ(booted.answer(), std::string{"20000"});
}

void test_cls_and_unknown_statements() {
    Booted booted;
    booted.run("PRINT \"BEFORE\"");
    TW_CHECK(booted.shows("BEFORE"));
    booted.run("CLS");
    TW_CHECK(booted.shows("KESTREL 16 BASIC 2.0"));
    TW_CHECK(!booted.shows("BEFORE"));
    booted.run("WIBBLE");
    TW_CHECK(booted.shows("?WHAT?"));
}

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
    Machine machine{Model::Kestrel16};
    TW_CHECK(!machine.mountKernal(std::vector<std::uint8_t>(100, 0), "short"));
    TW_CHECK(!machine.mountBasic(std::vector<std::uint8_t>(100, 0), "short"));
    TW_CHECK(machine.mountKernal(std::vector<std::uint8_t>(8192, 0), "kernal"));
    TW_CHECK(machine.mountBasic(std::vector<std::uint8_t>(65536, 0), "basic"));
}

void test_the_disk_is_a_sector_store() {
    Machine machine{Model::Kestrel16};
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
    TW_CHECK_EQ(booted.machine().video().cellAt(0, 0), std::uint8_t{'K'});
    TW_CHECK_EQ(booted.machine().read(0x008000), std::uint8_t{'K'});
    // Writing a cell through the bus reaches the tube.
    booted.machine().write(0x008000, 'X');
    TW_CHECK_EQ(booted.machine().video().cellAt(0, 0), std::uint8_t{'X'});
}

}  // namespace

int main() {
    test_one_machine_two_models();
    test_the_machine_boots();
    test_arithmetic_at_the_prompt();
    test_variables_and_strings();
    test_sixteen_bit_numbers();
    test_cls_and_unknown_statements();
    test_the_keyboard_reaches_the_screen();
    test_a_line_is_collected();
    test_a_wrong_sized_rom_is_refused();
    test_the_disk_is_a_sector_store();
    test_the_frame_and_the_keyboard();
    test_the_screen_is_the_roms();
    return tw::test::summary("kestrel16_machine_tests");
}
