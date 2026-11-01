// SPDX-License-Identifier: MIT
//
// The HB-1 as a machine: it boots the ROMs, takes keys, prints on the tube, and
// moves sectors. Every check here is about what the 6502 in the ROM did -- the
// C++ only types and reads the screen.

#include "check.hpp"

#include "timewarp/machine.hpp"
#include "timewarp/hb1/map.hpp"
#include "timewarp/hb1/rom.hpp"

#include <cstdio>
#include <string>
#include <string_view>

namespace {

using timewarp::Disk;
using timewarp::Machine;
using timewarp::Model;
using timewarp::hb1::RomInfo;
using timewarp::hb1::findRom;
using timewarp::hb1::map::kAttributeBase;
using timewarp::hb1::map::kScreenBase;

/// Types a line the way a person does: one key at a time, letting the machine
/// run in between, because the keyboard controller holds sixteen keys and a
/// fast typist would overflow it.
void typeOut(Machine& machine, std::string_view text) {
    for (char ch : text) {
        machine.type(static_cast<std::uint8_t>(ch));
        machine.runFrames(1);
    }
    machine.type(static_cast<std::uint8_t>(13));
    machine.runFrames(30);
}

/// The line on the screen that follows the one beginning with `after`.
std::string answerAfter(const Machine& machine, std::string_view after) {
    for (int row = 0; row + 1 < 25; ++row) {
        if (machine.screenLine(row) == after) {
            return machine.screenLine(row + 1);
        }
    }
    return {};
}

/// Everything on the tube, one line after another.
std::string screenText(const Machine& machine) {
    std::string all;
    for (int row = 0; row < 25; ++row) {
        all += machine.screenLine(row);
        all += '\n';
    }
    return all;
}

void test_the_machine_boots_into_the_rom() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    TW_CHECK((machine.screenLine(0)) == "*** HB-1 BASIC V2.0 ROM ***");
    TW_CHECK((machine.screenLine(1)) == "64K RAM SYSTEM");
    TW_CHECK((machine.screenLine(2)) == "(C) 1982 HB SYSTEMS");
    TW_CHECK((machine.screenLine(4)) == "READY.");
    // A real processor ran the ROM: it fetched instructions, and none of them
    // were undocumented.
    TW_CHECK(machine.cpu6502().instructionCount() > 10000);
    TW_CHECK_EQ(static_cast<int>(machine.cpu6502().illegalOpcodes()), 0);
}

void test_the_keyboard_echoes_and_edits() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "HELLO");
    TW_CHECK((machine.screenLine(5)) == "HELLO");
    // A line with a mistake in it, corrected with the Rub Out key.
    for (char ch : std::string{"ABCD"}) {
        machine.type(static_cast<std::uint8_t>(ch));
        machine.runFrames(1);
    }
    machine.type(8);   // Rub Out
    machine.type(static_cast<std::uint8_t>('Z'));
    machine.runFrames(4);
    TW_CHECK(machine.screenText().find("ABCZ") != std::string::npos);
}

void test_arithmetic_is_the_roms() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "PRINT 2+2");
    TW_CHECK((answerAfter(machine, "PRINT 2+2")) == "4");
    typeOut(machine, "PRINT 10/4");
    TW_CHECK((answerAfter(machine, "PRINT 10/4")) == "2.5");
    typeOut(machine, "PRINT 3*7-1");
    TW_CHECK((answerAfter(machine, "PRINT 3*7-1")) == "20");
    typeOut(machine, "PRINT (2+3)*(4-1)");
    TW_CHECK((answerAfter(machine, "PRINT (2+3)*(4-1)")) == "15");
    typeOut(machine, "PRINT 5-8");
    TW_CHECK((answerAfter(machine, "PRINT 5-8")) == "-3");
    typeOut(machine, "PRINT 100*100");
    TW_CHECK((answerAfter(machine, "PRINT 100*100")) == "10000");
    typeOut(machine, "PRINT 1/3");
    TW_CHECK((answerAfter(machine, "PRINT 1/3")) == "0.333333333");
    typeOut(machine, "PRINT 1E3+1");
    TW_CHECK((answerAfter(machine, "PRINT 1E3+1")) == "1001");
    TW_CHECK_EQ(static_cast<int>(machine.cpu6502().illegalOpcodes()), 0);
}

void test_variables_and_statements() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "LET X=6");
    typeOut(machine, "PRINT X*7");
    TW_CHECK((answerAfter(machine, "PRINT X*7")) == "42");
    typeOut(machine, "X=8");
    typeOut(machine, "PRINT X");
    TW_CHECK((answerAfter(machine, "PRINT X")) == "8");
}

void test_strings_and_poke() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "PRINT \"HELLO\"");
    TW_CHECK(machine.screenText().find("HELLO") != std::string::npos);
    // A string and a number in one PRINT, separated by a semicolon.
    typeOut(machine, "PRINT \"X=\";2+3");
    TW_CHECK(machine.screenText().find("X=5") != std::string::npos);
    // POKE writes through the machine's memory.
    typeOut(machine, "POKE 8452,42");
    TW_CHECK_EQ(static_cast<int>(machine.read(8452)), 42);
    // CLS clears the tube; the prompt then draws at the top left, so a cell
    // further down is the one that proves the screen was emptied.
    typeOut(machine, "CLS");
    TW_CHECK_EQ(static_cast<int>(machine.video().cell(500)), 32);
}

void test_errors_are_reported_by_the_rom() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "PRINT 1/0");
    TW_CHECK(machine.screenText().find("?DIVISION BY ZERO ERROR") != std::string::npos);
    typeOut(machine, "FROBNICATE");
    TW_CHECK(machine.screenText().find("?UNDEF'D STATEMENT ERROR") != std::string::npos);
    // The machine is still alive after an error.
    typeOut(machine, "PRINT 1+1");
    TW_CHECK((answerAfter(machine, "PRINT 1+1")) == "2");
}

void test_the_screen_is_memory() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    // The ROM wrote through the same addresses a program pokes.
    TW_CHECK_EQ(static_cast<char>(machine.read(kScreenBase)), '*');
    machine.write(kScreenBase, static_cast<std::uint8_t>('#'));
    const std::string firstRow = machine.screenLine(0);
    TW_CHECK(firstRow[0] == '#');
    // Colour attributes live in their own page.
    machine.write(kAttributeBase, 0x0A);
    TW_CHECK_EQ(static_cast<int>(machine.video().attribute(0)), 0x0A);
    // ROM is read-only.
    const std::uint8_t before = machine.read(0xE000);
    machine.write(0xE000, 0x00);
    TW_CHECK_EQ(static_cast<int>(machine.read(0xE000)), static_cast<int>(before));
}

void test_the_disk_moves_sectors() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    // Put a pattern in RAM, write it to sector 9 through the controller, clear
    // it, and read it back.
    constexpr std::uint16_t buffer = 0x4000;
    for (int i = 0; i < 256; ++i) {
        machine.write(static_cast<std::uint16_t>(buffer + i),
                      static_cast<std::uint8_t>(i ^ 0x5A));
    }
    machine.write(0x880C, 9);          // sector low
    machine.write(0x880D, 0);          // sector high
    machine.write(0x8810, buffer & 0xFF);
    machine.write(0x8811, buffer >> 8);
    machine.write(0x880E, 2);          // write
    TW_CHECK_EQ(static_cast<int>(machine.read(0x880F)), 0);

    for (int i = 0; i < 256; ++i) {
        machine.write(static_cast<std::uint16_t>(buffer + i), 0);
    }
    machine.write(0x880E, 1);          // read
    TW_CHECK_EQ(static_cast<int>(machine.read(0x880F)), 0);
    bool same = true;
    for (int i = 0; i < 256; ++i) {
        if (machine.read(static_cast<std::uint16_t>(buffer + i)) !=
            static_cast<std::uint8_t>(i ^ 0x5A)) {
            same = false;
        }
    }
    TW_CHECK(same);
    // The volume the machine came up with is named and has its sector count.
    TW_CHECK(machine.disk().volumeName() == "HBWARP");
    TW_CHECK_EQ(static_cast<int>(machine.read(0x8812)) | (static_cast<int>(machine.read(0x8813)) << 8),
                machine.profile().diskSectors);
}

void test_the_frame_counter_is_a_clock() {
    Machine machine{Model::Hb1};
    machine.runFrames(120);
    TW_CHECK(machine.frameCounter() >= 100);
    // The KERNAL reads it; a program can too.
    const std::uint16_t low = machine.read(0x8809);
    const std::uint16_t high = machine.read(0x880A);
    TW_CHECK_EQ(static_cast<int>((high << 8) | low),
                static_cast<int>(machine.frameCounter()));
}

void test_sound_is_a_latch() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    int heard = 0;
    machine.onSound = [&](std::uint8_t) { ++heard; };
    machine.write(0x8808, 40);
    TW_CHECK_EQ(heard, 1);
    TW_CHECK_EQ(static_cast<int>(machine.soundLatch()), 40);
    TW_CHECK_EQ(static_cast<int>(machine.notesPlayed()), 1);
}

void test_the_machine_is_configurable() {
    // Speed: the 6502 is always the same; what changes is how many of its
    // cycles the host gives it per second.
    {
        Machine machine{Model::Hb1};
        TW_CHECK(machine.cyclesPerSecond() == 1'000'000);
        machine.setSpeed(Machine::Speed::Mhz4);
        TW_CHECK(machine.cyclesPerSecond() == 4'000'000);
        machine.setSpeed(Machine::Speed::Turbo);
        TW_CHECK(machine.cyclesPerSecond() == 0);  // as fast as the host
    }

    // Sound is a switch on the host, not on the ROM.
    {
        Machine machine{Model::Hb1};
        machine.runFrames(20);
        int heard = 0;
        machine.onSound = [&](std::uint8_t) { ++heard; };
        machine.setSoundEnabled(false);
        machine.write(0x8808, 12);
        TW_CHECK(heard == 0);
        TW_CHECK(static_cast<int>(machine.notesPlayed()) == 1);  // the latch moved
        machine.setSoundEnabled(true);
        machine.write(0x8808, 13);
        TW_CHECK(heard == 1);
    }

    // The socket takes another ROM: the monitor boots instead of BASIC.
    {
        Machine machine{Model::Hb1};
        const RomInfo* monitor = findRom("monitor");
        TW_CHECK(monitor != nullptr);
        if (monitor != nullptr) {
            TW_CHECK(machine.mountSocketRom(*monitor->image, monitor->id));
            TW_CHECK(machine.socketRomId() == "monitor");
            machine.runFrames(20);
            TW_CHECK(machine.screenText().find("HB-1 MONITOR V1.0") != std::string::npos);
            TW_CHECK_EQ(static_cast<int>(machine.cpu6502().illegalOpcodes()), 0);
            // And back to BASIC without a restart.
            const RomInfo* basic = findRom("basic");
            TW_CHECK(basic != nullptr);
            if (basic != nullptr) {
                machine.mountSocketRom(*basic->image, basic->id);
                machine.runFrames(20);
                TW_CHECK(machine.screenText().find("HB-1 BASIC V2.0") != std::string::npos);
            }
        }
        // A ROM of the wrong size is refused, and the machine keeps its own.
        std::vector<std::uint8_t> small(1024, 0xEA);
        TW_CHECK(!machine.mountSocketRom(small, "small"));
        TW_CHECK(machine.socketRomId() == "basic");
    }

    // A cartridge appears at $9000 and is read-only.
    {
        Machine machine{Model::Hb1};
        std::vector<std::uint8_t> cart(4096, 0);
        cart[0] = 0xA9;  // LDA #
        cart[1] = 0x42;
        TW_CHECK(machine.mountCartridge(cart, "demo"));
        TW_CHECK(machine.cartridgeMounted());
        TW_CHECK_EQ(static_cast<int>(machine.read(0x9000)), 0xA9);
        machine.write(0x9000, 0x00);
        TW_CHECK_EQ(static_cast<int>(machine.read(0x9000)), 0xA9);
        machine.ejectCartridge();
        TW_CHECK(!machine.cartridgeMounted());
        TW_CHECK_EQ(static_cast<int>(machine.read(0x9000)), 0);
        // The cartridge is not allowed to be the wrong size either.
        std::vector<std::uint8_t> wrong(1234, 0);
        TW_CHECK(!machine.mountCartridge(wrong, "wrong"));
    }
}

void test_the_cassette_records_and_plays_back() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    machine.tape().insert();
    TW_CHECK((machine.read(0x8819) & 0x80) != 0);  // a tape is present

    machine.write(0x881A, 2);  // record
    TW_CHECK((machine.read(0x8819) & 0x20) != 0);
    for (std::uint8_t value : {std::uint8_t{0xDE}, std::uint8_t{0xAD}, std::uint8_t{0xBE}}) {
        machine.write(0x8818, value);
    }
    machine.write(0x881A, 3);  // stop
    TW_CHECK_EQ(static_cast<int>(machine.tape().bytes().size()), 3);

    machine.write(0x881A, 4);  // rewind
    machine.write(0x881A, 1);  // play
    TW_CHECK((machine.read(0x8819) & 0x10) != 0);      // something to read
    TW_CHECK_EQ(static_cast<int>(machine.read(0x8818)), 0xDE);
    TW_CHECK_EQ(static_cast<int>(machine.read(0x8818)), 0xAD);
    TW_CHECK_EQ(static_cast<int>(machine.read(0x8818)), 0xBE);
    TW_CHECK((machine.read(0x8819) & 0x10) == 0);      // nothing left
}


/// The program in memory is the program the interpreter runs: lines linked in
/// number order, each with its own number in its header.
void test_a_program_is_stored_as_linked_lines() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "20 PRINT \"B\"");
    typeOut(machine, "10 PRINT \"A\"");   // typed out of order on purpose
    typeOut(machine, "30 END");

    const auto lineAt = [&machine](std::uint16_t at) {
        return static_cast<int>(machine.read(at)) |
               (static_cast<int>(machine.read(static_cast<std::uint16_t>(at + 1))) << 8);
    };
    const std::uint16_t first = 0x0801;
    const auto second = static_cast<std::uint16_t>(lineAt(first));
    TW_CHECK(second != 0);
    TW_CHECK_EQ(lineAt(static_cast<std::uint16_t>(first + 2)), 10);
    TW_CHECK_EQ(lineAt(static_cast<std::uint16_t>(second + 2)), 20);
    const auto third = static_cast<std::uint16_t>(lineAt(second));
    TW_CHECK_EQ(lineAt(static_cast<std::uint16_t>(third + 2)), 30);
    // the last line ends the list
    TW_CHECK_EQ(lineAt(third), 0);
    // and the program says what it holds
    typeOut(machine, "LIST");
    const std::string all = screenText(machine);
    TW_CHECK(all.find("10 PRINT \"A\"") != std::string::npos);
    TW_CHECK(all.find("20 PRINT \"B\"") != std::string::npos);
    TW_CHECK(all.find("30 END") != std::string::npos);
}

/// Running a program: statements in order, loops, subroutines and jumps.
void test_a_program_runs() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "10 FOR I=1 TO 3");
    typeOut(machine, "20 PRINT I");
    typeOut(machine, "30 NEXT I");
    typeOut(machine, "40 GOSUB 100");
    typeOut(machine, "50 IF I=4 THEN PRINT \"OK\"");
    typeOut(machine, "60 GOTO 200");
    typeOut(machine, "70 PRINT \"NO\"");
    typeOut(machine, "100 PRINT \"SUB\"");
    typeOut(machine, "110 RETURN");
    typeOut(machine, "200 PRINT \"DONE\"");
    typeOut(machine, "CLS");
    typeOut(machine, "RUN");
    const std::string all = screenText(machine);
    TW_CHECK(all.find("1\n2\n3\nSUB\nOK\nDONE") != std::string::npos);
    TW_CHECK(all.find("NO") == std::string::npos);
}

/// READ takes its values out of the DATA statements, in order, across lines.
void test_read_takes_values_from_data() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "10 DATA 1,2");
    typeOut(machine, "20 READ A,B");
    typeOut(machine, "30 DATA 3");
    typeOut(machine, "40 READ C");
    typeOut(machine, "50 PRINT A;B;C");
    typeOut(machine, "RUN");
    TW_CHECK((answerAfter(machine, "RUN")) == "123");
}

/// DELETE squeezes the lines it is given out of the program.
void test_delete_removes_lines() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "10 PRINT \"A\"");
    typeOut(machine, "20 PRINT \"B\"");
    typeOut(machine, "30 PRINT \"C\"");
    typeOut(machine, "DELETE 20");
    typeOut(machine, "CLS");
    typeOut(machine, "LIST");
    const std::string all = screenText(machine);
    TW_CHECK(all.find("10 PRINT \"A\"") != std::string::npos);
    TW_CHECK(all.find("20 PRINT \"B\"") == std::string::npos);
    TW_CHECK(all.find("30 PRINT \"C\"") != std::string::npos);
    typeOut(machine, "RUN");
    TW_CHECK((answerAfter(machine, "RUN")) == "A");
}

/// The functions, which live in the ROM beside the interpreter.
void test_the_functions() {
    struct Case {
        const char* line;
        const char* answer;
    };
    // Each is typed into a fresh machine so that the screen never scrolls the
    // question away before the answer is read.
    const Case cases[] = {
        {"PRINT SQR(2)", "1.41421356"},
        {"PRINT INT(-7.8)", "-8"},
        {"PRINT INT(7)", "7"},
        {"PRINT ABS(0-5)", "5"},
        {"PRINT SGN(0-3)", "-1"},
        {"PRINT 2^10", "1024"},
        {"PRINT 2^-1", "0.5"},
        {"PRINT EXP(0)", "1"},
        {"PRINT INT(LOG(2)*1000)", "693"},
        {"PRINT INT(EXP(1)*100)", "271"},
        {"PRINT SIN(0)", "0"},
        {"PRINT COS(0)", "1"},
        {"PRINT ATN(0)", "0"},
        {"PRINT PEEK(32768) >= 0", "-1"},
    };
    for (const auto& one : cases) {
        Machine machine{Model::Hb1};
        machine.runFrames(20);
        typeOut(machine, one.line);
        TW_CHECK((answerAfter(machine, one.line)) == one.answer);
    }
}

/// A program goes out to the disk and comes back, and CATALOG sees it.
void test_the_disk_holds_a_program() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "10 PRINT \"FROM DISK\"");
    typeOut(machine, "SAVE \"DEMO\"");
    typeOut(machine, "NEW");
    typeOut(machine, "CLS");
    typeOut(machine, "LIST");
    TW_CHECK(screenText(machine).find("FROM DISK") == std::string::npos);
    typeOut(machine, "CATALOG");
    TW_CHECK(screenText(machine).find("DEMO") != std::string::npos);
    typeOut(machine, "LOAD \"DEMO\"");
    typeOut(machine, "RUN");
    TW_CHECK((answerAfter(machine, "RUN")) == "FROM DISK");
}


/// String variables and the functions that work on them.
void test_strings() {
    struct Case {
        const char* line;
        const char* answer;
    };
    const Case cases[] = {
        {"A$=\"HELLO\"", ""},
        {"PRINT A$", "HELLO"},
        {"PRINT LEN(A$)", "5"},
        {"PRINT LEFT$(A$,3)", "HEL"},
        {"PRINT RIGHT$(A$,2)", "LO"},
        {"PRINT MID$(A$,2,3)", "ELL"},
        {"PRINT MID$(A$,4)", "LO"},
        {"PRINT ASC(A$)", "72"},
        {"PRINT CHR$(65);CHR$(66)", "AB"},
        {"PRINT STR$(42)", "42"},
        {"PRINT VAL(\"12.5\")*2", "25"},
        {"B$=\"\":PRINT LEN(B$)", "0"},
    };
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    for (const auto& one : cases) {
        typeOut(machine, one.line);
        if (one.answer[0] == '\0') {
            continue;
        }
        TW_CHECK((answerAfter(machine, one.line)) == one.answer);
    }
    // A string where a number belongs, and the other way round, are both named.
    Machine other{Model::Hb1};
    other.runFrames(20);
    typeOut(other, "A$=5");
    TW_CHECK(screenText(other).find("TYPE MISMATCH") != std::string::npos);
    Machine third{Model::Hb1};
    third.runFrames(20);
    typeOut(third, "X=\"HI\"");
    TW_CHECK(screenText(third).find("TYPE MISMATCH") != std::string::npos);
}

/// Strings in a program: built, printed and measured as the 1982 listings did.
void test_strings_in_a_program() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "10 A$=\"CAT\"");
    typeOut(machine, "20 PRINT A$;\" \";LEN(A$);\" \";LEFT$(A$,2)");
    typeOut(machine, "30 FOR I=1 TO 3:PRINT CHR$(64+I);:NEXT:PRINT");
    typeOut(machine, "CLS");
    typeOut(machine, "RUN");
    const std::string all = screenText(machine);
    TW_CHECK(all.find("CAT 3 CA") != std::string::npos);
    TW_CHECK(all.find("A\nB\nC") != std::string::npos);
}

/// GET takes one key without waiting for Return.
void test_get_takes_one_key() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "A$=\"?\"");
    typeOut(machine, "GET A$");
    machine.type(static_cast<std::uint8_t>('K'));
    machine.runFrames(10);
    typeOut(machine, "PRINT A$");
    TW_CHECK((answerAfter(machine, "PRINT A$")) == "K");
}


/// Arrays: dimensioned, filled, read back, and refused when out of range.
void test_arrays() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "DIM A(5)");
    typeOut(machine, "A(0)=10:A(5)=50");
    typeOut(machine, "PRINT A(0);A(5);A(3)");
    TW_CHECK((answerAfter(machine, "PRINT A(0);A(5);A(3)")) == "10500");
    typeOut(machine, "FOR I=0 TO 5:A(I)=I*I:NEXT");
    typeOut(machine, "PRINT A(1);A(2);A(3)");
    TW_CHECK((answerAfter(machine, "PRINT A(1);A(2);A(3)")) == "149");

    // Two dimensions, the last subscript moving fastest.
    typeOut(machine, "DIM M(2,2)");
    typeOut(machine, "FOR I=0 TO 2:FOR J=0 TO 2:M(I,J)=I*10+J:NEXT:NEXT");
    typeOut(machine, "PRINT M(0,1);M(1,0);M(2,2)");
    TW_CHECK((answerAfter(machine, "PRINT M(0,1);M(1,0);M(2,2)")) == "11022");

    // READ fills an element like any other variable.
    Machine read{Model::Hb1};
    read.runFrames(20);
    typeOut(read, "10 DATA 7,8,9");
    typeOut(read, "20 DIM A(2):FOR I=0 TO 2:READ A(I):NEXT");
    typeOut(read, "30 PRINT A(0);A(1);A(2)");
    typeOut(read, "CLS");
    typeOut(read, "RUN");
    TW_CHECK((answerAfter(read, "RUN")) == "789");

    // A name used before it is dimensioned gets eleven elements.
    typeOut(machine, "B(3)=9");
    typeOut(machine, "PRINT B(3)");
    TW_CHECK((answerAfter(machine, "PRINT B(3)")) == "9");

    // And the two ways an array can be wrong are named.
    Machine over{Model::Hb1};
    over.runFrames(20);
    typeOut(over, "DIM A(5)");
    typeOut(over, "A(6)=1");
    TW_CHECK(screenText(over).find("BAD SUBSCRIPT") != std::string::npos);

    Machine twice{Model::Hb1};
    twice.runFrames(20);
    typeOut(twice, "DIM A(5)");
    typeOut(twice, "DIM A(7)");
    TW_CHECK(screenText(twice).find("REDIMENSIONED ARRAY") != std::string::npos);
}


/// Strings compare and join, which is how 1982 programs were written.
void test_strings_compare_and_join() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "A$=\"APPLE\"");
    typeOut(machine, "B$=\"BANANA\"");
    typeOut(machine, "PRINT A$=A$");
    TW_CHECK((answerAfter(machine, "PRINT A$=A$")) == "-1");
    typeOut(machine, "PRINT A$=B$");
    TW_CHECK((answerAfter(machine, "PRINT A$=B$")) == "0");
    typeOut(machine, "PRINT A$<B$");
    TW_CHECK((answerAfter(machine, "PRINT A$<B$")) == "-1");
    typeOut(machine, "PRINT B$<A$");
    TW_CHECK((answerAfter(machine, "PRINT B$<A$")) == "0");
    typeOut(machine, "PRINT A$<>B$");
    TW_CHECK((answerAfter(machine, "PRINT A$<>B$")) == "-1");
    // a shorter string sorts first
    typeOut(machine, "C$=\"AB\"");
    typeOut(machine, "PRINT C$<A$");
    TW_CHECK((answerAfter(machine, "PRINT C$<A$")) == "-1");

    typeOut(machine, "D$=A$+\" \"+B$");
    typeOut(machine, "PRINT D$");
    TW_CHECK((answerAfter(machine, "PRINT D$")) == "APPLE BANANA");
    typeOut(machine, "PRINT LEN(D$)");
    TW_CHECK((answerAfter(machine, "PRINT LEN(D$)")) == "12");
    typeOut(machine, "E$=MID$(D$,1,5)+\"-\"+RIGHT$(D$,6)");
    typeOut(machine, "PRINT E$");
    TW_CHECK((answerAfter(machine, "PRINT E$")) == "APPLE-BANANA");

    // and a program can branch on a string, which is the point of comparing
    Machine program{Model::Hb1};
    program.runFrames(20);
    typeOut(program, "10 A$=\"Y\"");
    typeOut(program, "20 IF A$=\"Y\" THEN PRINT \"YES\"");
    typeOut(program, "30 IF A$<>\"Y\" THEN PRINT \"NO\"");
    typeOut(program, "CLS");
    typeOut(program, "RUN");
    TW_CHECK((answerAfter(program, "RUN")) == "YES");
    TW_CHECK(screenText(program).find("NO\n") == std::string::npos);
}


/// DATA holds strings as well as numbers, and READ hands them to either kind.
void test_read_takes_strings_too() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "10 DATA \"APPLE\",\"PEAR\",42");
    typeOut(machine, "20 READ A$,B$,N");
    typeOut(machine, "30 PRINT A$;\" \";B$;\" \";N");
    typeOut(machine, "40 PRINT LEN(A$)+LEN(B$)");
    typeOut(machine, "CLS");
    typeOut(machine, "RUN");
    TW_CHECK((answerAfter(machine, "RUN")) == "APPLE PEAR 42");
    TW_CHECK((answerAfter(machine, "APPLE PEAR 42")) == "9");
}

/// STOP says where it stopped and CONT carries on from there.
void test_stop_and_continue() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "10 PRINT \"ONE\"");
    typeOut(machine, "20 STOP");
    typeOut(machine, "30 PRINT \"TWO\"");
    typeOut(machine, "CLS");
    typeOut(machine, "RUN");
    const std::string stopped = screenText(machine);
    TW_CHECK(stopped.find("ONE") != std::string::npos);
    TW_CHECK(stopped.find("BREAK IN 20") != std::string::npos);
    TW_CHECK(stopped.find("TWO") == std::string::npos);
    typeOut(machine, "CONT");
    const std::string carried = screenText(machine);
    TW_CHECK(carried.find("TWO") != std::string::npos);
    // and there is only one go at it
    typeOut(machine, "CONT");
    TW_CHECK(screenText(machine).find("CAN'T CONTINUE") != std::string::npos);
}


/// DEF FN: a definition in a program, called from an expression.
void test_defined_functions() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "10 DEF FN A(X)=X*X+1");
    typeOut(machine, "20 DEF FN B(Y)=Y/2");
    typeOut(machine, "30 PRINT FN A(3)");
    typeOut(machine, "40 PRINT FN A(FN A(2))");
    typeOut(machine, "50 PRINT FN B(10)+FN A(1)");
    typeOut(machine, "CLS");
    typeOut(machine, "RUN");
    TW_CHECK((answerAfter(machine, "RUN")) == "10");
    TW_CHECK((answerAfter(machine, "10")) == "26");
    TW_CHECK((answerAfter(machine, "26")) == "7");
    // a name that was never defined is its own error
    Machine missing{Model::Hb1};
    missing.runFrames(20);
    typeOut(missing, "PRINT FN Z(1)");
    TW_CHECK(screenText(missing).find("UNDEF'D FUNCTION") != std::string::npos);
}

/// Arrays of strings hold strings, and keep clear of the numeric ones.
void test_string_arrays() {
    Machine machine{Model::Hb1};
    machine.runFrames(20);
    typeOut(machine, "DIM A$(3)");
    typeOut(machine, "A$(0)=\"ZERO\":A$(2)=\"TWO\"");
    typeOut(machine, "PRINT A$(0);\"-\";A$(2);\"-\";LEN(A$(2))");
    TW_CHECK((answerAfter(machine, "PRINT A$(0);\"-\";A$(2);\"-\";LEN(A$(2))")) == "ZERO-TWO-3");
    typeOut(machine, "DIM A(2)");
    typeOut(machine, "A(1)=9");
    typeOut(machine, "PRINT A(1);A$(0)");
    TW_CHECK((answerAfter(machine, "PRINT A(1);A$(0)")) == "9ZERO");
    // two dimensions, and READ into a string array
    Machine grid{Model::Hb1};
    grid.runFrames(20);
    typeOut(grid, "DIM M$(1,1)");
    typeOut(grid, "M$(0,1)=\"R1C1\":M$(1,0)=\"R2C0\"");
    typeOut(grid, "PRINT M$(0,1);\"|\";M$(1,0)");
    TW_CHECK((answerAfter(grid, "PRINT M$(0,1);\"|\";M$(1,0)")) == "R1C1|R2C0");
    Machine read{Model::Hb1};
    read.runFrames(20);
    typeOut(read, "10 DATA \"A\",\"BB\",\"CCC\"");
    typeOut(read, "20 DIM N$(2):FOR I=0 TO 2:READ N$(I):NEXT");
    typeOut(read, "30 PRINT N$(0);N$(1);N$(2)");
    typeOut(read, "CLS");
    typeOut(read, "RUN");
    TW_CHECK((answerAfter(read, "RUN")) == "ABBCCC");
}

}  // namespace

int main() {
    test_the_machine_boots_into_the_rom();
    test_the_keyboard_echoes_and_edits();
    test_arithmetic_is_the_roms();
    test_variables_and_statements();
    test_strings_and_poke();
    test_errors_are_reported_by_the_rom();
    test_the_screen_is_memory();
    test_the_disk_moves_sectors();
    test_the_frame_counter_is_a_clock();
    test_sound_is_a_latch();
    test_the_machine_is_configurable();
    test_the_cassette_records_and_plays_back();
    test_a_program_is_stored_as_linked_lines();
    test_a_program_runs();
    test_read_takes_values_from_data();
    test_delete_removes_lines();
    test_the_functions();
    test_the_disk_holds_a_program();
    test_strings();
    test_strings_in_a_program();
    test_get_takes_one_key();
    test_arrays();
    test_strings_compare_and_join();
    test_read_takes_strings_too();
    test_stop_and_continue();
    test_defined_functions();
    test_string_arrays();
    return tw::test::summary("hb1_machine_tests");
}
