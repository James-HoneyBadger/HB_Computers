// SPDX-License-Identifier: MIT
//
// The Kestrel's processor: a MOS 6502, cycle-counted.
//
// The machine's languages are not C++ interpreters: they are 6502 machine code
// assembled into the ROM images in `roms/kestrel/`, and this is the core that
// executes them. Every `PRINT`, every `GOTO`, and every error message on the
// tube comes out of a byte the ROM put there.
//
// The core implements the documented instruction set. Undocumented opcodes are
// counted rather than executed: a ROM that lands on one has a bug, and the
// counter is how a test says so. Cycle counts are the documented ones,
// including the page-crossing and branch-taken penalties, because the machine
// is paced at 1 MHz and a program's idea of time has to be the real one: the
// frame interrupt arrives every 20 000 cycles whether the CPU is busy or not.

#pragma once

#include <cstdint>

namespace timewarp::kestrel {

/// The 64 KiB address space as the processor sees it.
class Bus {
public:
    virtual ~Bus() = default;
    [[nodiscard]] virtual std::uint8_t read(std::uint16_t address) = 0;
    virtual void write(std::uint16_t address, std::uint8_t value) = 0;
};

class Cpu6502 {
public:
    // Status register bits, named the way the data sheets name them.
    static constexpr std::uint8_t kCarry = 0x01;
    static constexpr std::uint8_t kZero = 0x02;
    static constexpr std::uint8_t kInterruptDisable = 0x04;
    static constexpr std::uint8_t kDecimal = 0x08;
    static constexpr std::uint8_t kBreak = 0x10;
    static constexpr std::uint8_t kUnused = 0x20;
    static constexpr std::uint8_t kOverflow = 0x40;
    static constexpr std::uint8_t kNegative = 0x80;

    /// What the processor is holding. A snapshot, for the monitor and tests.
    struct Registers {
        std::uint8_t a = 0;
        std::uint8_t x = 0;
        std::uint8_t y = 0;
        std::uint8_t sp = 0xFD;
        std::uint8_t status = 0x24;
        std::uint16_t pc = 0;
    };

    explicit Cpu6502(Bus& bus) : bus_(bus) {}

    /// The power-on state: PC from the reset vector, stack trimmed, interrupts
    /// off until the ROM enables them.
    void reset();
    [[nodiscard]] Registers registers() const;
    void setRegisters(const Registers& registers);

    /// The maskable interrupt line. Level-triggered, as on the real chip: the
    /// line stays high until the device that raised it is serviced.
    void setIrqLine(bool level) { irqLine_ = level; }
    [[nodiscard]] bool irqLine() const noexcept { return irqLine_; }
    /// A non-maskable interrupt (the BREAK key). Edge-triggered: one request is
    /// one interrupt, however long it waits.
    void requestNmi() { nmiPending_ = true; }

    /// Executes one instruction, or services a pending interrupt. Returns the
    /// cycles it took.
    int step();

    [[nodiscard]] std::uint64_t cycleCount() const noexcept { return cyclesTotal_; }
    [[nodiscard]] std::uint64_t instructionCount() const noexcept {
        return instructions_;
    }
    /// How many times the processor fetched an undocumented opcode. A working
    /// ROM leaves this at zero.
    [[nodiscard]] std::uint64_t illegalOpcodes() const noexcept {
        return illegalOpcodes_;
    }
    [[nodiscard]] std::uint8_t lastIllegalOpcode() const noexcept {
        return lastIllegalOpcode_;
    }

private:
    struct Effective {
        std::uint16_t address = 0;
        bool crossedPage = false;
    };

    [[nodiscard]] std::uint8_t fetch();
    [[nodiscard]] std::uint16_t fetchWord();
    [[nodiscard]] std::uint16_t readWord(std::uint16_t address);
    /// The 6502's indirect-jump page bug, kept because a ROM that depends on it
    /// depends on the real chip's behaviour.
    [[nodiscard]] std::uint16_t readWordBug(std::uint16_t address);

    [[nodiscard]] Effective addressZeroPage();
    [[nodiscard]] Effective addressZeroPageX();
    [[nodiscard]] Effective addressZeroPageY();
    [[nodiscard]] Effective addressAbsolute();
    [[nodiscard]] Effective addressAbsoluteX();
    [[nodiscard]] Effective addressAbsoluteY();
    [[nodiscard]] Effective addressIndexedIndirect();
    [[nodiscard]] Effective addressIndirectIndexed();
    [[nodiscard]] std::uint16_t addressIndirect();

    void push(std::uint8_t value);
    [[nodiscard]] std::uint8_t pull();

    [[nodiscard]] bool flag(std::uint8_t bit) const {
        return (status_ & bit) != 0;
    }
    void setFlag(std::uint8_t bit, bool on) {
        if (on) {
            status_ |= bit;
        } else {
            status_ &= static_cast<std::uint8_t>(~bit);
        }
    }
    void setZeroNegative(std::uint8_t value) {
        setFlag(kZero, value == 0);
        setFlag(kNegative, (value & 0x80) != 0);
    }

    void adc(std::uint8_t value);
    void sbc(std::uint8_t value);
    void compare(std::uint8_t reg, std::uint8_t value);
    [[nodiscard]] std::uint8_t asl(std::uint8_t value);
    [[nodiscard]] std::uint8_t lsr(std::uint8_t value);
    [[nodiscard]] std::uint8_t rol(std::uint8_t value);
    [[nodiscard]] std::uint8_t ror(std::uint8_t value);

    void interrupt(std::uint16_t vector);
    void jam(std::uint8_t opcode);

    Bus& bus_;
    std::uint8_t a_ = 0;
    std::uint8_t x_ = 0;
    std::uint8_t y_ = 0;
    std::uint8_t status_ = 0x24;
    std::uint8_t sp_ = 0xFD;
    std::uint16_t pc_ = 0;

    bool irqLine_ = false;
    bool nmiPending_ = false;

    std::uint64_t cyclesTotal_ = 0;
    std::uint64_t instructions_ = 0;
    std::uint64_t illegalOpcodes_ = 0;
    std::uint8_t lastIllegalOpcode_ = 0;
};

}  // namespace timewarp::kestrel
