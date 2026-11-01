// SPDX-License-Identifier: MIT
//
// The Kestrel's address map, in one place.
//
// This header and `roms/kestrel/kestrel.inc` are the same map written twice: the ROM
// has to know where its own registers are, and the emulator has to decode the
// same addresses. When one changes, the other changes in the same commit --
// `kestrel_machine_tests` checks the pair by poking each register through the
// address the ROM uses.
//
//      $0000 .. $00FF   zero page
//      $0100 .. $01FF   the processor's stack
//      $0200 .. $07FF   system RAM: input buffer, workspace
//      $0800 .. $7FFF   BASIC program, variables and free RAM (30 KiB)
//      $8000 .. $83E7   screen matrix, 40 x 25, row-major
//      $8400 .. $87E7   colour attributes, one (ink, paper) byte per cell
//      $8800 .. $88FF   I/O registers
//      $8900 .. $9FFF   nothing responds: reads are 0, writes are lost
//      $A000 .. $DFFF   BASIC ROM (16 KiB)
//      $E000 .. $FFFF   KERNAL ROM (8 KiB, with the three vectors in the top six
//                       bytes, where the processor looks for them)

#pragma once

#include <cstdint>

namespace timewarp::kestrel::map {

inline constexpr std::uint16_t kRamBase = 0x0000;
inline constexpr std::uint16_t kRamTop = 0x7FFF;

inline constexpr std::uint16_t kScreenBase = 0x8000;
inline constexpr std::uint16_t kScreenColumns = 40;
inline constexpr std::uint16_t kScreenRows = 25;
inline constexpr std::uint16_t kScreenCells =
    kScreenColumns * kScreenRows;  // 1000

inline constexpr std::uint16_t kAttributeBase = 0x8400;
inline constexpr std::uint16_t kAttributeCells = kScreenCells;

inline constexpr std::uint16_t kIoBase = 0x8800;

/// A 4 KiB cartridge: the expansion a 1982 machine took in its slot.
inline constexpr std::uint16_t kCartridgeBase = 0x9000;
inline constexpr std::uint16_t kCartridgeSize = 0x1000;

inline constexpr std::uint16_t kBasicRomBase = 0xA000;
inline constexpr std::uint16_t kBasicRomSize = 0x4000;

inline constexpr std::uint16_t kKernalRomBase = 0xE000;
inline constexpr std::uint16_t kKernalRomSize = 0x2000;

// Registers in the I/O page, as offsets from kIoBase. A write is an output to a
// device and a read is an input from one; the page is not storage.
enum Register : std::uint16_t {
    /// The next keystroke. Reading takes it out of the buffer; 0 means empty.
    kKeyboardData = 0x00,
    /// Bit 7 set while a keystroke is waiting.
    kKeyboardStatus = 0x01,
    /// Writing anything clears the keyboard buffer.
    kKeyboardClear = 0x02,

    kCursorColumn = 0x03,
    kCursorRow = 0x04,
    kInk = 0x05,
    kPaper = 0x06,
    kBorder = 0x07,

    /// A write sounds the note; reading answers 0, so a program can tell it was
    /// heard.
    kSound = 0x08,

    /// The frame counter, a 16-bit count of 50 Hz ticks.
    kFrameLow = 0x09,
    kFrameHigh = 0x0A,
    /// Set by the machine when a frame passed; cleared by writing 0.
    kFrameFlag = 0x0B,

    /// Disk: a 19-bit sector number, a buffer in RAM, and a command.
    kDiskSectorLow = 0x0C,
    kDiskSectorHigh = 0x0D,
    /// 1 = read the sector into the buffer, 2 = write the buffer to the sector,
    /// 3 = report the volume's sector count.
    kDiskCommand = 0x0E,
    /// Bit 7 set when a command failed; bits 0..6 the last command's result.
    kDiskStatus = 0x0F,
    kDiskBufferLow = 0x10,
    kDiskBufferHigh = 0x11,
    /// The number of sectors on the mounted volume, low byte then high.
    kDiskCountLow = 0x12,
    kDiskCountHigh = 0x13,

    /// A byte from the machine's noise generator; reading advances it.
    kRandom = 0x14,

    /// The last key pressed, latched, for a program that wants to watch one key
    /// rather than drain a buffer.
    kLastKey = 0x15,
    /// Set by the machine when the screen scrolled past a row; a program clears
    /// it by writing 0.
    kScrolled = 0x16,
    /// 1 while the cursor is lit. The KERNAL blinks it from the frame interrupt.
    kCursorOn = 0x17,

    /// The cassette: writing a byte puts it on the tape, reading takes the next
    /// one off, and the status byte says whether the motor is on and whether
    /// anything is left.
    kTapeData = 0x18,
    kTapeStatus = 0x19,
    kTapeControl = 0x1A,
};

// Video colour codes: the low nibble of an attribute is the paper and the high
// nibble the ink, exactly as `POKE` writes them.
enum Colour : std::uint8_t {
    kBlack = 0,
    kWhite = 1,
    kRed = 2,
    kCyan = 3,
    kPurple = 4,
    kGreen = 5,
    kBlue = 6,
    kYellow = 7,
    kOrange = 8,
    kBrown = 9,
    kLightRed = 10,
    kDarkGrey = 11,
    kGrey = 12,
    kLightGreen = 13,
    kLightBlue = 14,
    kLightGrey = 15,
};

}  // namespace timewarp::kestrel::map
