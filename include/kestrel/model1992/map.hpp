// SPDX-License-Identifier: MIT
//
// The Kestrel 16's address map, in one place.
//
// This header and `roms/kestrel16/kestrel16.inc` are the same map written twice: the ROM
// has to know where its own registers are, and the emulator has to decode the
// same addresses. When one changes, the other changes in the same commit.
//
// The 65816 sees twenty-four address lines, so the machine is a row of 64 KiB
// banks rather than one page of memory. Bank zero keeps the shape the Kestrel had,
// which is what makes the two machines one family:
//
//   bank $00
//     $0000 .. $00FF   zero page
//     $0100 .. $01FF   the processor's stack
//     $0200 .. $07FF   system RAM: input buffer, workspace
//     $0800 .. $7FFF   BASIC program, variables and free RAM (30 KiB)
//     $8000 .. $8F9F   screen characters, 80 x 50, row-major
//     $8FA0 .. $9F3F   attributes, one (ink, paper) byte per cell
//     $A000 .. $A0FF   I/O registers
//     $A100 .. $BFFF   nothing responds
//     $C000 .. $DFFF   cartridge ROM (8 KiB)
//     $E000 .. $FFFF   KERNAL ROM (8 KiB, with the vectors in the top sixteen
//                      bytes of the bank, where the processor looks for them)
//   bank $01            BASIC ROM (64 KiB) -- a decade later the language is
//                       not squeezed into the same socket as the kernel
//   banks $04 .. $07    a cartridge, 256 KiB
//   bank $00, $08..$0F  RAM: one megabyte in all
//
// Everything else is open bus: reads are zero and writes are lost.

#pragma once

#include <cstdint>

namespace kestrel::model1992::map {

inline constexpr std::uint16_t kRamBase = 0x0000;
inline constexpr std::uint16_t kRamTop = 0x7FFF;

inline constexpr std::uint16_t kScreenBase = 0x8000;
inline constexpr std::uint16_t kScreenColumns = 80;
inline constexpr std::uint16_t kScreenRows = 50;
inline constexpr std::uint16_t kScreenCells = kScreenColumns * kScreenRows;

inline constexpr std::uint16_t kAttributeBase =
    static_cast<std::uint16_t>(kScreenBase + kScreenCells);

inline constexpr std::uint16_t kIoBase = 0xA000;

inline constexpr std::uint16_t kCartridgeBase = 0xC000;
inline constexpr std::uint16_t kCartridgeSize = 0x2000;

inline constexpr std::uint16_t kKernalRomBase = 0xE000;
inline constexpr std::uint16_t kKernalRomSize = 0x2000;

inline constexpr std::uint8_t kBasicBank = 0x01;
inline constexpr std::uint32_t kBasicRomBase = 0x010000;
inline constexpr std::uint32_t kBasicRomSize = 0x010000;

inline constexpr std::uint8_t kCartridgeFirstBank = 0x04;
inline constexpr std::uint8_t kCartridgeBankCount = 4;

/// The banks the machine answers for as RAM: bank zero and the eight above it.
inline constexpr std::uint8_t kRamBanks[] = {0x00, 0x08, 0x09, 0x0A,
                                             0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

inline constexpr std::uint32_t kDiskSectors = 4096;
inline constexpr std::uint16_t kDiskSectorSize = 512;

// The sixteen colours the tube can paint, as indexes into the host's palette.
// Bits 0-2 are the gun strengths and bit 3 is brightness, which is how a 1992
// attribute byte packs a colour into four bits.
enum Colour : std::uint8_t {
    kBlack = 0x0,
    kBlue = 0x1,
    kRed = 0x2,
    kMagenta = 0x3,
    kGreen = 0x4,
    kCyan = 0x5,
    kYellow = 0x6,
    kLightGrey = 0x7,
    kDarkGrey = 0x8,
    kLightBlue = 0x9,
    kLightRed = 0xA,
    kLightMagenta = 0xB,
    kLightGreen = 0xC,
    kLightCyan = 0xD,
    kLightYellow = 0xE,
    kWhite = 0xF,
};

// Registers in the I/O page, as offsets from kIoBase. A write is an output to a
// device and a read is an input from one; the page is not storage.
enum Register : std::uint16_t {
    /// The next keystroke. Reading takes it out of the buffer.
    kKeyboardData = 0x00,
    /// Bit 7 set while a keystroke is waiting.
    kKeyboardStatus = 0x01,
    /// Writing anything clears the keyboard buffer.
    kKeyboardClear = 0x02,
    kCursorColumn = 0x03,
    kCursorRow = 0x04,
    /// The two colours a new character is drawn in, as attribute nibbles.
    kInk = 0x05,
    kPaper = 0x06,
    kBorder = 0x07,
    /// The noise latch. A 1992 machine still has one byte for sound.
    kSound = 0x08,
    /// The frame counter, 50 per second, low byte first.
    kFrameLow = 0x09,
    kFrameHigh = 0x0A,
    /// Set by the machine every frame; write anything to clear it.
    kFrameFlag = 0x0B,
    /// Which sector the next disk command uses.
    kDiskSectorLow = 0x0C,
    kDiskSectorHigh = 0x0D,
    /// 1 read, 2 write, 3 format, 4 query.
    kDiskCommand = 0x0E,
    /// Bit 0 busy, bit 1 error, bit 2 present, bit 7 done.
    kDiskStatus = 0x0F,
    /// Where in the address space a sector goes or comes from: a full 24-bit
    /// address, low byte first, because a megabyte does not fit a pointer.
    kDiskAddressLow = 0x10,
    kDiskAddressHigh = 0x11,
    kDiskAddressBank = 0x12,
    /// How many sectors to move.
    kDiskCountLow = 0x13,
    kDiskCountHigh = 0x14,
    kRandom = 0x15,
    kLastKey = 0x16,
    kScrolled = 0x17,
    kCursorOn = 0x18,
    kTapeData = 0x19,
    kTapeStatus = 0x1A,
    kTapeControl = 0x1B,
};

// The tape's status byte.
enum TapeStatus : std::uint8_t {
    kTapePresent = 0x80,
    kTapePlaying = 0x40,
    kTapeRecording = 0x20,
    kTapeDataLeft = 0x10,
};

// The tape's control byte.
enum TapeCommand : std::uint8_t {
    kTapePlay = 1,
    kTapeRecord = 2,
    kTapeStop = 3,
    kTapeRewind = 4,
};

// The disk's commands.
enum DiskCommand : std::uint8_t {
    kDiskRead = 1,
    kDiskWrite = 2,
    kDiskFormat = 3,
    kDiskQuery = 4,
};

// The disk's status bits.
enum DiskStatus : std::uint8_t {
    kDiskBusy = 0x01,
    kDiskError = 0x02,
    kDiskPresent = 0x04,
    kDiskDone = 0x80,
};

}  // namespace kestrel::model1992::map
