// SPDX-License-Identifier: MIT
//
// The HB-2: one 65816, one megabyte of RAM, a row of ROM banks, the tube, the
// keyboard, the disk and the tape.
//
// This class is the machine and nothing else. It has no idea what BASIC is: it
// decodes addresses, ticks a 50 Hz frame, hands the processor a keystroke and
// lets it write to the screen. Everything a person sees the machine do -- the
// banner, the prompt, `PRINT 2+2`, the error messages -- is the ROMs executing
// at 8 MHz.
//
// The host's whole job is `runCycles`, `type` and reading the screen back.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "timewarp/hb2/cpu65816.hpp"
#include "timewarp/hb2/disk.hpp"
#include "timewarp/hb2/keyboard.hpp"
#include "timewarp/hb2/map.hpp"
#include "timewarp/hb2/tape.hpp"
#include "timewarp/hb2/video.hpp"

namespace timewarp::hb2 {

/// The keys the machine knows by name. The printable ones arrive as their own
/// ASCII code, so a program that asks for a character gets what a person typed.
enum class SpecialKey : std::uint8_t {
    Return = 13,
    Delete = 8,
    Escape = 27,
    Up = 0x91,
    Down = 0x11,
    Left = 0x9D,
    Right = 0x1D,
    Break = 0x03,
};

class Hb2Machine final : public Bus {
public:
    /// Eight megahertz, fifty frames a second: 160 000 cycles a frame, which is
    /// the budget every piece of ROM timing is written against.
    static constexpr int kClockHz = 8'000'000;
    static constexpr int kFramesPerSecond = 50;
    static constexpr int kCyclesPerFrame = kClockHz / kFramesPerSecond;

    /// How fast the host clocks the machine. The processor is always the same
    /// 65816; this is how many of its cycles the host gives it per second.
    enum class Speed { Mhz1, Mhz2, Mhz8, Turbo };

    Hb2Machine();

    // -- configuration ------------------------------------------------------

    void setSpeed(Speed speed) noexcept { speed_ = speed; }
    [[nodiscard]] Speed speed() const noexcept { return speed_; }
    [[nodiscard]] int cyclesPerSecond() const noexcept;

    void setSoundEnabled(bool enabled) noexcept { soundEnabled_ = enabled; }
    [[nodiscard]] bool soundEnabled() const noexcept { return soundEnabled_; }

    /// The KERNAL in the socket: 8 KiB mapped at $00:E000. The machine takes a
    /// copy and does not reset by itself.
    bool mountKernal(const std::vector<std::uint8_t>& image,
                     std::string_view id);
    [[nodiscard]] std::string_view kernalId() const noexcept { return kernalId_; }

    /// The language ROM: 64 KiB in bank one.
    bool mountBasic(const std::vector<std::uint8_t>& image,
                    std::string_view id);
    [[nodiscard]] std::string_view basicId() const noexcept { return basicId_; }
    [[nodiscard]] const std::vector<std::uint8_t>& basicRom() const noexcept {
        return basicRom_;
    }

    /// A cartridge in banks four to seven, or none.
    bool mountCartridge(const std::vector<std::uint8_t>& image,
                        std::string_view id);
    void ejectCartridge() noexcept;
    [[nodiscard]] bool cartridgeMounted() const noexcept {
        return cartridgeMounted_;
    }
    [[nodiscard]] std::string_view cartridgeId() const noexcept {
        return cartridgeId_;
    }

    // -- the bus ------------------------------------------------------------

    [[nodiscard]] std::uint8_t read(std::uint32_t address) override;
    void write(std::uint32_t address, std::uint8_t value) override;

    // -- running ------------------------------------------------------------

    /// Power-on: RAM cleared, video cleared, ROMs in place, processor reset.
    void reset();
    void runCycles(long cycles);
    void runFrames(int frames);

    // -- the host's hands ---------------------------------------------------

    void type(std::uint8_t key);
    void typeText(std::string_view text);
    void typeLine(std::string_view text);
    /// The BREAK key: a non-maskable interrupt, which the KERNAL turns into a
    /// warm start.
    void pressBreak();

    // -- reading the machine ------------------------------------------------

    [[nodiscard]] std::string screenText() const { return video_.text(); }
    [[nodiscard]] std::string screenLine(int row) const {
        return video_.line(row);
    }
    [[nodiscard]] int cursorColumn() const noexcept { return cursorColumn_; }
    [[nodiscard]] int cursorRow() const noexcept { return cursorRow_; }
    [[nodiscard]] std::uint8_t ink() const noexcept { return ink_; }
    [[nodiscard]] std::uint8_t paper() const noexcept { return paper_; }
    [[nodiscard]] std::uint8_t border() const noexcept { return border_; }
    [[nodiscard]] std::uint16_t frameCounter() const noexcept {
        return frameCounter_;
    }
    [[nodiscard]] bool cursorOn() const noexcept { return cursorOn_; }
    [[nodiscard]] std::uint8_t soundLatch() const noexcept {
        return soundLatch_;
    }
    [[nodiscard]] std::uint64_t notesPlayed() const noexcept {
        return notesPlayed_;
    }
    [[nodiscard]] bool keyAvailable() const { return keyboard_.available(); }

    [[nodiscard]] Hb2Video& video() noexcept { return video_; }
    [[nodiscard]] const Hb2Video& video() const noexcept { return video_; }
    [[nodiscard]] Hb2Keyboard& keyboard() noexcept { return keyboard_; }
    [[nodiscard]] Hb2Disk& disk() noexcept { return disk_; }
    [[nodiscard]] const Hb2Disk& disk() const noexcept { return disk_; }
    [[nodiscard]] Hb2Tape& tape() noexcept { return tape_; }
    [[nodiscard]] const Hb2Tape& tape() const noexcept { return tape_; }
    [[nodiscard]] Cpu65816& cpu() noexcept { return cpu_; }
    [[nodiscard]] const Cpu65816& cpu() const noexcept { return cpu_; }

    /// Called when the ROM writes the sound latch, so the host can make the
    /// noise. The machine itself has no audio output -- it has a latch.
    std::function<void(std::uint8_t)> onSound;

    /// Direct access to a RAM bank, for tests and for the monitor.
    [[nodiscard]] std::uint8_t ramAt(std::uint32_t address) const;
    void setRamAt(std::uint32_t address, std::uint8_t value);

    /// The noise generator, one byte at a time. Deterministic, so a test that
    /// depends on `RND` gets the same numbers every run.
    std::uint8_t randomByte();

private:
    void tickFrame();
    void executeDiskCommand(std::uint8_t command);
    [[nodiscard]] bool interruptPending() const {
        return keyboard_.available() || frameFlag_;
    }
    [[nodiscard]] std::uint8_t readRegister(std::uint16_t offset);
    void writeRegister(std::uint16_t offset, std::uint8_t value);

    /// Banks zero through fifteen, one megabyte, in address order.
    std::vector<std::uint8_t> ram_;
    Cpu65816 cpu_{*this};
    Hb2Video video_;
    Hb2Keyboard keyboard_;
    Hb2Disk disk_;
    Hb2Tape tape_;

    std::uint8_t cursorColumn_ = 0;
    std::uint8_t cursorRow_ = 0;
    bool cursorOn_ = false;
    bool scrolled_ = false;
    std::uint8_t ink_ = map::kLightGrey;
    std::uint8_t paper_ = map::kBlack;
    std::uint8_t border_ = map::kLightBlue;

    std::uint16_t frameCounter_ = 0;
    bool frameFlag_ = false;
    std::uint32_t cyclesToFrame_ = 0;

    std::uint8_t soundLatch_ = 0;
    std::uint64_t notesPlayed_ = 0;

    std::uint16_t randomState_ = 0xACE1;
    std::uint8_t lastKey_ = 0;

    // Disk command registers.
    std::uint32_t diskSector_ = 0;
    std::uint32_t diskAddress_ = 0;
    std::uint16_t diskCount_ = 0;
    std::uint8_t diskStatus_ = 0;

    Speed speed_ = Speed::Mhz8;
    bool soundEnabled_ = true;

    std::vector<std::uint8_t> kernal_;
    std::string kernalId_ = "kernal";
    std::vector<std::uint8_t> basicRom_;
    std::string basicId_ = "basic";
    std::vector<std::uint8_t> cartridge_;
    std::string cartridgeId_;
    bool cartridgeMounted_ = false;
};

}  // namespace timewarp::hb2
