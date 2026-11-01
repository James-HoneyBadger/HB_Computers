// SPDX-License-Identifier: MIT
//
// The HB-1: one 6502, one 64K, one tube, one keyboard, one disk, two ROMs.
//
// This class is the machine and nothing else. It has no idea what BASIC is: it
// decodes addresses, ticks a 50 Hz frame, hands the processor a keystroke and
// lets it write to the screen. Everything a person sees the machine do -- the
// banner, the prompt, `PRINT 2+2`, the error messages, `CATALOG` -- is the ROMs
// executing at 1 MHz.
//
// The host's whole job is `runCycles`, `type` and reading the screen back.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <string_view>

#include "timewarp/hb1/cpu6502.hpp"
#include "timewarp/hb1/disk.hpp"
#include "timewarp/hb1/keyboard.hpp"
#include "timewarp/hb1/map.hpp"
#include "timewarp/hb1/tape.hpp"
#include "timewarp/hb1/video.hpp"

namespace timewarp::hb1 {

class Hb1Machine final : public Bus {
public:
    /// The machine's clock is 1 MHz and its frame is 50 Hz, so a frame is
    /// 20 000 cycles. Everything time-dependent in the ROM counts frames.
    static constexpr int kClockHz = 1'000'000;
    static constexpr int kFramesPerSecond = 50;
    static constexpr int kCyclesPerFrame = kClockHz / kFramesPerSecond;

    /// How fast the host clocks the machine. The processor is always the same
    /// 6502; this is how many of its cycles the host gives it per second, which
    /// is what "1 MHz" or "as fast as this computer can" means to a person
    /// watching the screen.
    enum class Speed { Mhz1, Mhz2, Mhz4, Turbo };

    Hb1Machine();

    // -- configuration ------------------------------------------------------

    void setSpeed(Speed speed) noexcept { speed_ = speed; }
    [[nodiscard]] Speed speed() const noexcept { return speed_; }
    /// Cycles per second the host should run: 0 means "as fast as possible".
    [[nodiscard]] int cyclesPerSecond() const noexcept;

    /// Sound is a latch on the machine and a choice on the host; muting does
    /// not stop the ROM writing to it.
    void setSoundEnabled(bool enabled) noexcept { soundEnabled_ = enabled; }
    [[nodiscard]] bool soundEnabled() const noexcept { return soundEnabled_; }

    /// The ROM in the socket. The image must be 16 KiB; the machine takes a
    /// copy, resets, and the new ROM is what the reset vector points into.
    bool mountSocketRom(const std::vector<std::uint8_t>& image,
                        std::string_view id);
    [[nodiscard]] std::string_view socketRomId() const noexcept {
        return socketRomId_;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& socketRom() const noexcept {
        return socketRom_;
    }

    /// A 4 KiB cartridge at $9000, or none.
    bool mountCartridge(const std::vector<std::uint8_t>& image,
                        std::string_view id);
    void ejectCartridge() noexcept;
    [[nodiscard]] bool cartridgeMounted() const noexcept {
        return cartridgeMounted_;
    }
    [[nodiscard]] std::string_view cartridgeId() const noexcept {
        return cartridgeId_;
    }

    [[nodiscard]] Hb1Tape& tape() noexcept { return tape_; }
    [[nodiscard]] const Hb1Tape& tape() const noexcept { return tape_; }

    // -- the bus ----------------------------------------------------------

    [[nodiscard]] std::uint8_t read(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;

    // -- running ----------------------------------------------------------

    /// Power-on: RAM cleared, video cleared, ROM in place, processor reset.
    void reset();
    /// Runs the processor for at least `cycles` cycles, ticking frames as the
    /// count passes them.
    void runCycles(long cycles);
    void runFrames(int frames);

    // -- the host's hands --------------------------------------------------

    /// Queues a keystroke, as the keyboard controller would receive it.
    void type(std::uint8_t key);
    void typeText(std::string_view text);
    /// Types a line and presses Return. Newline characters in `text` become the
    /// Return key.
    void typeLine(std::string_view text);
    /// The BREAK key: a non-maskable interrupt, which the KERNAL turns into a
    /// warm start.
    void pressBreak();

    // -- reading the machine ----------------------------------------------

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

    [[nodiscard]] Hb1Video& video() noexcept { return video_; }
    [[nodiscard]] const Hb1Video& video() const noexcept { return video_; }
    [[nodiscard]] Hb1Keyboard& keyboard() noexcept { return keyboard_; }
    [[nodiscard]] Hb1Disk& disk() noexcept { return disk_; }
    [[nodiscard]] const Hb1Disk& disk() const noexcept { return disk_; }
    [[nodiscard]] Cpu6502& cpu() noexcept { return cpu_; }
    [[nodiscard]] const Cpu6502& cpu() const noexcept { return cpu_; }

    /// Called when the ROM writes the sound latch, so the host can make the
    /// noise. The machine itself has no audio output -- it has a latch.
    std::function<void(std::uint8_t)> onSound;

    /// True while a keystroke is waiting to be read.
    [[nodiscard]] bool keyAvailable() const { return keyboard_.available(); }

    /// The noise generator, one byte at a time. Deterministic: a test that
    /// depends on `RND` gets the same numbers every run.
    std::uint8_t randomByte();

private:
    void tickFrame();
    void executeDiskCommand(std::uint8_t command);
    [[nodiscard]] bool interruptPending() const {
        return keyboard_.available() || frameFlag_;
    }

    std::array<std::uint8_t, static_cast<std::size_t>(map::kRamTop) + 1> ram_{};
    Cpu6502 cpu_{*this};
    Hb1Video video_;
    Hb1Keyboard keyboard_;
    Hb1Disk disk_;

    std::uint16_t cursorColumn_ = 0;
    std::uint16_t cursorRow_ = 0;
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

    // Disk command registers.
    std::uint32_t diskSector_ = 0;
    std::uint16_t diskBuffer_ = 0;
    std::uint8_t diskStatus_ = 0;
    std::uint16_t diskSectorCount_ = 0;

    Speed speed_ = Speed::Mhz1;
    bool soundEnabled_ = true;
    std::vector<std::uint8_t> socketRom_;
    std::string socketRomId_ = "basic";
    std::vector<std::uint8_t> cartridge_{};
    std::string cartridgeId_{};
    bool cartridgeMounted_ = false;
    Hb1Tape tape_;
};

}  // namespace timewarp::hb1
