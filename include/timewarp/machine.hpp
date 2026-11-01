// SPDX-License-Identifier: MIT
//
// The machine: one emulator with two models.
//
// The HB-1 and the HB-2 are the same design ten years apart, so they are the same
// program with different numbers. A `Machine` is built for a `Model`, and the
// profile table says what that model is: its clock, its memory, where its screen
// and its registers are, how big its ROMs are, and how its disk is divided.
//
// Everything a person sees the machine do is machine code in a ROM image that
// this class executes: the host supplies a processor, memory, a tube, a keyboard
// and a disk, and nothing else. The host's whole job is `runCycles`, `type` and
// reading the screen back.
//
// The two processors are the two the machines really had -- a 6502 and a 65C816 --
// and each is used whole. What is shared is everything around them: the frame,
// the register file, the tube, the keyboard, the disk and the cassette.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "timewarp/devices.hpp"
#include "timewarp/hb1/cpu6502.hpp"
#include "timewarp/hb2/cpu65816.hpp"

namespace timewarp {

/// Which machine this is.
enum class Model { Hb1, Hb2 };

/// Everything that differs between the two, in one table.
struct Profile {
    Model model;
    const char* name;

    int clockHz;
    int framesPerSecond;
    int cyclesPerFrame;

    /// Bytes of RAM the machine answers for, from address zero up.
    int ramBytes;

    std::uint32_t screenBase;
    int columns;
    int rows;
    std::uint32_t attributeBase;

    std::uint32_t ioBase;
    std::uint32_t ioSize;

    std::uint32_t cartridgeBase;
    int cartridgeSize;

    /// The KERNAL's window in bank zero, and the language ROM's address, which
    /// is a bank of its own on the HB-2 and a window beside the KERNAL on the
    /// HB-1.
    std::uint32_t kernalBase;
    int kernalSize;
    std::uint32_t basicBase;
    int basicSize;

    int diskSectors;
    int diskSectorSize;
    int keyboardCapacity;
};

[[nodiscard]] const Profile& profileOf(Model model);

class Machine final : public hb2::Bus {
public:
    /// How fast the host clocks the machine. The processor is always the same
    /// one; this is how many of its cycles the host gives it per second, which
    /// is what "8 MHz" or "as fast as this computer can" means to a person
    /// watching the screen.
    enum class Speed { Mhz1, Mhz2, Mhz4, Mhz8, Turbo };

    explicit Machine(Model model = Model::Hb1);

    [[nodiscard]] Model model() const noexcept { return profile_.model; }
    [[nodiscard]] const Profile& profile() const noexcept { return profile_; }
    [[nodiscard]] const char* modelName() const noexcept { return profile_.name; }

    // -- configuration ------------------------------------------------------

    void setSpeed(Speed speed) noexcept { speed_ = speed; }
    [[nodiscard]] Speed speed() const noexcept { return speed_; }
    /// Cycles per second the host should run: 0 means "as fast as possible".
    [[nodiscard]] int cyclesPerSecond() const noexcept;

    void setSoundEnabled(bool enabled) noexcept { soundEnabled_ = enabled; }
    [[nodiscard]] bool soundEnabled() const noexcept { return soundEnabled_; }

    /// The KERNAL: the part of the machine that is always there. Its size is the
    /// profile's, and an image of the wrong size is refused.
    bool mountKernal(const std::vector<std::uint8_t>& image,
                     std::string_view id);
    [[nodiscard]] std::string_view kernalId() const noexcept { return kernalId_; }
    [[nodiscard]] const std::vector<std::uint8_t>& kernalRom() const noexcept {
        return kernal_;
    }

    /// The language: a socket ROM on the HB-1, a bank of its own on the HB-2.
    bool mountBasic(const std::vector<std::uint8_t>& image,
                    std::string_view id);
    [[nodiscard]] std::string_view basicId() const noexcept { return basicId_; }
    [[nodiscard]] const std::vector<std::uint8_t>& basicRom() const noexcept {
        return basic_;
    }

    /// Puts a ROM in the HB-1's language socket. The machine resets, which is
    /// what a socket with a new chip in it does: the reset vector in the new
    /// image is where the processor starts.
    bool mountSocketRom(const std::vector<std::uint8_t>& image,
                        std::string_view id) {
        if (!mountBasic(image, id)) {
            return false;
        }
        reset();
        return true;
    }
    [[nodiscard]] std::string_view socketRomId() const noexcept {
        return basicId_;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& socketRom() const noexcept {
        return basic_;
    }

    bool mountCartridge(const std::vector<std::uint8_t>& image,
                        std::string_view id);
    void ejectCartridge() noexcept;
    [[nodiscard]] bool cartridgeMounted() const noexcept {
        return cartridgeMounted_;
    }
    [[nodiscard]] std::string_view cartridgeId() const noexcept {
        return cartridgeId_;
    }

    /// Loads the ROMs the build produced for this model.
    void mountShippedRoms();

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

    [[nodiscard]] Video& video() noexcept { return video_; }
    [[nodiscard]] const Video& video() const noexcept { return video_; }
    [[nodiscard]] Keyboard& keyboard() noexcept { return keyboard_; }
    [[nodiscard]] Disk& disk() noexcept { return disk_; }
    [[nodiscard]] const Disk& disk() const noexcept { return disk_; }
    [[nodiscard]] Tape& tape() noexcept { return tape_; }
    [[nodiscard]] const Tape& tape() const noexcept { return tape_; }

    /// The processor, as the model's own type. Asking a 6502 machine for the
    /// 65816 is a mistake the type system should catch, so there is one accessor
    /// for each and no common one.
    [[nodiscard]] hb1::Cpu6502& cpu6502() noexcept { return cpu6502_; }
    [[nodiscard]] const hb1::Cpu6502& cpu6502() const noexcept { return cpu6502_; }
    [[nodiscard]] hb2::Cpu65816& cpu65816() noexcept { return cpu65816_; }
    [[nodiscard]] const hb2::Cpu65816& cpu65816() const noexcept {
        return cpu65816_;
    }

    /// Called when the ROM writes the sound latch, so the host can make the
    /// noise. The machine itself has no audio output -- it has a latch.
    std::function<void(std::uint8_t)> onSound;

    /// The noise generator, one byte at a time. Deterministic, so a test that
    /// depends on `RND` gets the same numbers every run.
    std::uint8_t randomByte();

    [[nodiscard]] std::uint8_t ramAt(std::uint32_t address) const;
    void setRamAt(std::uint32_t address, std::uint8_t value);

private:
    /// What a register in the I/O page is. The two machines put the same
    /// devices at slightly different offsets, so the profile fills a table once
    /// and the decoder switches on the kind rather than on the number.
    enum class Reg : std::uint8_t {
        None,
        KeyboardData,
        KeyboardStatus,
        KeyboardClear,
        CursorColumn,
        CursorRow,
        Ink,
        Paper,
        Border,
        Sound,
        FrameLow,
        FrameHigh,
        FrameFlag,
        DiskSectorLow,
        DiskSectorHigh,
        DiskCommand,
        DiskStatus,
        DiskAddressLow,
        DiskAddressHigh,
        DiskAddressBank,
        DiskCountLow,
        DiskCountHigh,
        Random,
        LastKey,
        Scrolled,
        CursorOn,
        TapeData,
        TapeStatus,
        TapeControl,
    };

    /// The 6502's view: sixteen address lines, into the same bus.
    class Bus16 final : public hb1::Bus {
    public:
        explicit Bus16(Machine& machine) : machine_(machine) {}
        [[nodiscard]] std::uint8_t read(std::uint16_t address) override {
            return machine_.read(address);
        }
        void write(std::uint16_t address, std::uint8_t value) override {
            machine_.write(address, value);
        }

    private:
        Machine& machine_;
    };

    void layOutRegisters();
    void tickFrame();
    void executeDiskCommand(std::uint8_t command);
    [[nodiscard]] bool interruptPending() const {
        return keyboard_.available() || frameFlag_;
    }
    [[nodiscard]] std::uint8_t readRegister(Reg kind);
    void writeRegister(Reg kind, std::uint8_t value);

    Profile profile_;
    Bus16 bus16_{*this};
    hb1::Cpu6502 cpu6502_{bus16_};
    hb2::Cpu65816 cpu65816_{*this};

    std::vector<std::uint8_t> ram_;
    Video video_;
    Keyboard keyboard_;
    Disk disk_;
    Tape tape_;

    std::array<Reg, 256> registers_{};

    std::uint8_t cursorColumn_ = 0;
    std::uint8_t cursorRow_ = 0;
    bool cursorOn_ = false;
    bool scrolled_ = false;
    std::uint8_t ink_;
    std::uint8_t paper_;
    std::uint8_t border_;

    std::uint16_t frameCounter_ = 0;
    bool frameFlag_ = false;
    std::uint32_t cyclesToFrame_ = 0;

    std::uint8_t soundLatch_ = 0;
    std::uint64_t notesPlayed_ = 0;

    std::uint16_t randomState_ = 0xACE1;
    std::uint8_t lastKey_ = 0;

    // Disk command registers, wide enough for either machine's answer.
    std::uint32_t diskSector_ = 0;
    std::uint32_t diskAddress_ = 0;
    std::uint16_t diskCount_ = 0;
    std::uint8_t diskStatus_ = 0;

    Speed speed_ = Speed::Mhz1;
    bool soundEnabled_ = true;

    std::vector<std::uint8_t> kernal_;
    std::vector<std::uint8_t> basic_;
    std::vector<std::uint8_t> cartridge_;
    std::string kernalId_ = "kernal";
    std::string basicId_ = "basic";
    std::string cartridgeId_;
    bool cartridgeMounted_ = false;
};

}  // namespace timewarp
