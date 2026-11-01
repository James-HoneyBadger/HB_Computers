// SPDX-License-Identifier: MIT
//
// The HB-2's processor: a WDC 65C816, cycle-counted.
//
// The HB-1 put its languages in a 6502 ROM socket; the HB-2 is the same idea ten
// years later, and the processor the decade actually shipped is this one. The
// machine's KERNAL and its BASIC are assembled into the ROM images in
// `roms/hb2/` by `hb2asm` and executed here, so every character on the screen
// came out of a byte the ROM put there.
//
// This is the full instruction set: emulation and native modes, the eight- and
// sixteen-bit accumulator and index sizes selected by the M and X bits of the
// status register, the direct page and stack pointer that can be anywhere in
// bank zero, the data bank that decides where an absolute address points, and
// the program bank that decides where the next instruction comes from.
//
// Cycle counts are the documented ones. Emulation mode is not separately timed:
// a machine that boots into it and leaves inside its first hundred instructions
// is paced accurately enough by the native counts, and the frame interrupt that
// drives the screen is counted in cycles, not in instructions.

#pragma once

#include <cstdint>

namespace timewarp::hb2 {

/// The 16 MiB address space as the processor sees it: one flat 24-bit number.
/// Which bank that number names is the machine's business, not the core's.
class Bus {
public:
    virtual ~Bus() = default;
    [[nodiscard]] virtual std::uint8_t read(std::uint32_t address) = 0;
    virtual void write(std::uint32_t address, std::uint8_t value) = 0;
};

class Cpu65816 {
public:
    // Status register bits, named the way the data sheets name them. The two
    // that are not flags in the usual sense are the sizes: when a bit is set,
    // the matching register is eight bits wide.
    static constexpr std::uint8_t kCarry = 0x01;
    static constexpr std::uint8_t kZero = 0x02;
    static constexpr std::uint8_t kIrqDisable = 0x04;
    static constexpr std::uint8_t kDecimal = 0x08;
    static constexpr std::uint8_t kIndex8 = 0x10;
    static constexpr std::uint8_t kMemory8 = 0x20;
    static constexpr std::uint8_t kOverflow = 0x40;
    static constexpr std::uint8_t kNegative = 0x80;

    /// What the processor is holding. A snapshot, for the monitor and tests.
    struct Registers {
        std::uint16_t c = 0;
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        std::uint16_t s = 0x01FF;
        std::uint16_t d = 0;
        std::uint16_t pc = 0;
        std::uint8_t dbr = 0;
        std::uint8_t pbr = 0;
        std::uint8_t p = 0x34;
        bool emulation = true;
    };

    explicit Cpu65816(Bus& bus) : bus_(bus) {}

    /// The power-on state: emulation mode, as the chip leaves reset, with PC
    /// from the reset vector in bank zero.
    void reset();
    [[nodiscard]] Registers registers() const;
    void setRegisters(const Registers& registers);

    /// The maskable interrupt line, level-triggered.
    void setIrqLine(bool level) { irqLine_ = level; }
    [[nodiscard]] bool irqLine() const noexcept { return irqLine_; }
    /// A non-maskable interrupt, edge-triggered.
    void requestNmi() { nmiPending_ = true; }

    /// Executes one instruction, or services a pending interrupt. Returns the
    /// cycles it took.
    int step();

    [[nodiscard]] std::uint64_t cycleCount() const noexcept { return cyclesTotal_; }
    [[nodiscard]] std::uint64_t instructionCount() const noexcept {
        return instructions_;
    }
    /// Set by `STP`; a stopped processor does nothing until it is reset.
    [[nodiscard]] bool stopped() const noexcept { return stopped_; }
    /// Set by `WAI`; cleared by the interrupt it is waiting for.
    [[nodiscard]] bool waiting() const noexcept { return waiting_; }
    /// How many times an unassigned opcode was executed. A working ROM leaves
    /// this at zero.
    [[nodiscard]] std::uint64_t illegalOpcodes() const noexcept {
        return illegalOpcodes_;
    }
    [[nodiscard]] std::uint8_t lastIllegalOpcode() const noexcept {
        return lastIllegalOpcode_;
    }

    /// The accumulator as the program sees it: eight bits or sixteen.
    [[nodiscard]] std::uint8_t cLow() const noexcept {
        return static_cast<std::uint8_t>(c_ & 0xFF);
    }
    [[nodiscard]] std::uint16_t c() const noexcept { return c_; }
    [[nodiscard]] bool emulation() const noexcept { return emulation_; }
    [[nodiscard]] std::uint8_t status() const noexcept { return p_; }

private:
    [[nodiscard]] bool m8() const noexcept {
        return emulation_ || (p_ & kMemory8) != 0;
    }
    [[nodiscard]] bool x8() const noexcept {
        return emulation_ || (p_ & kIndex8) != 0;
    }

    [[nodiscard]] bool flag(std::uint8_t bit) const { return (p_ & bit) != 0; }
    void setFlag(std::uint8_t bit, bool on) {
        if (on) {
            p_ |= bit;
        } else {
            p_ &= static_cast<std::uint8_t>(~bit);
        }
    }
    void setZeroNegative8(std::uint8_t value) {
        setFlag(kZero, value == 0);
        setFlag(kNegative, (value & 0x80) != 0);
    }
    void setZeroNegative16(std::uint16_t value) {
        setFlag(kZero, value == 0);
        setFlag(kNegative, (value & 0x8000) != 0);
    }

    [[nodiscard]] std::uint8_t read8(std::uint32_t address) {
        return bus_.read(address & 0xFFFFFF);
    }
    void write8(std::uint32_t address, std::uint8_t value) {
        bus_.write(address & 0xFFFFFF, value);
    }
    [[nodiscard]] std::uint16_t read16(std::uint32_t address);
    void write16(std::uint32_t address, std::uint16_t value);
    /// A sixteen-bit pointer read that cannot leave bank zero, for the
    /// indirect jumps.
    [[nodiscard]] std::uint16_t readPointer(std::uint16_t offset);

    [[nodiscard]] std::uint8_t fetchByte();
    [[nodiscard]] std::uint16_t fetchWord();
    [[nodiscard]] std::uint16_t fetchOperandData();
    [[nodiscard]] std::uint16_t fetchOperandIndex();

    void push8(std::uint8_t value);
    void push16(std::uint16_t value);
    [[nodiscard]] std::uint8_t pull8();
    [[nodiscard]] std::uint16_t pull16();

    [[nodiscard]] std::uint32_t dataAddress(std::uint16_t offset) const {
        return (static_cast<std::uint32_t>(dbr_) << 16) | offset;
    }
    [[nodiscard]] std::uint32_t codeAddress(std::uint16_t offset) const {
        return (static_cast<std::uint32_t>(pbr_) << 16) | offset;
    }

    [[nodiscard]] std::uint16_t readData(std::uint32_t address);
    void writeData(std::uint32_t address, std::uint16_t value);

    void adc(std::uint16_t value);
    void sbc(std::uint16_t value);
    void compare(std::uint16_t reg, std::uint16_t value);
    [[nodiscard]] std::uint16_t aslValue(std::uint16_t value);
    [[nodiscard]] std::uint16_t lsrValue(std::uint16_t value);
    [[nodiscard]] std::uint16_t rolValue(std::uint16_t value);
    [[nodiscard]] std::uint16_t rorValue(std::uint16_t value);

    void interrupt(std::uint16_t vectorOffset, bool brk);
    void exchangeEmulation();

    Bus& bus_;
    std::uint16_t c_ = 0;
    std::uint16_t x_ = 0;
    std::uint16_t y_ = 0;
    std::uint16_t s_ = 0x01FF;
    std::uint16_t d_ = 0;
    std::uint16_t pc_ = 0;
    std::uint8_t dbr_ = 0;
    std::uint8_t pbr_ = 0;
    std::uint8_t p_ = 0x34;
    bool emulation_ = true;
    bool stopped_ = false;
    bool waiting_ = false;

    bool irqLine_ = false;
    bool nmiPending_ = false;

    std::uint64_t cyclesTotal_ = 0;
    std::uint64_t instructions_ = 0;
    std::uint64_t illegalOpcodes_ = 0;
    std::uint8_t lastIllegalOpcode_ = 0;
};

}  // namespace timewarp::hb2
