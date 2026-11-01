// SPDX-License-Identifier: MIT
//
// The 65816 core: the two register widths, the direct page and the banks, the
// new addressing modes, the arithmetic, and the cycle counts the machine's
// real-time budget depends on.

#include "check.hpp"

#include "timewarp/hb2/cpu65816.hpp"

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace {

using timewarp::hb2::Bus;
using timewarp::hb2::Cpu65816;

/// Sixteen megabytes of plain memory, which is the whole address space.
class RamBus final : public Bus {
public:
    RamBus() : memory_(0x1000000, 0) {}

    [[nodiscard]] std::uint8_t read(std::uint32_t address) override {
        return memory_[address & 0xFFFFFF];
    }
    void write(std::uint32_t address, std::uint8_t value) override {
        memory_[address & 0xFFFFFF] = value;
    }

    void load(std::uint32_t address,
              std::initializer_list<std::uint8_t> bytes) {
        std::uint32_t at = address;
        for (const std::uint8_t byte : bytes) {
            memory_[at++] = byte;
        }
    }
    void poke(std::uint32_t address, std::uint8_t value) {
        memory_[address & 0xFFFFFF] = value;
    }
    [[nodiscard]] std::uint8_t peek(std::uint32_t address) const {
        return memory_[address & 0xFFFFFF];
    }
    [[nodiscard]] std::uint16_t peekWord(std::uint32_t address) const {
        return static_cast<std::uint16_t>(peek(address) |
                                          (peek(address + 1) << 8));
    }

private:
    std::vector<std::uint8_t> memory_;
};

/// A machine at a chosen starting state, with a little program in it.
struct Run {
    RamBus bus;
    Cpu65816 cpu{bus};

    Run(std::uint32_t origin, std::initializer_list<std::uint8_t> program,
        bool native = true) {
        bus.load(origin, program);
        Cpu65816::Registers registers;
        registers.pc = static_cast<std::uint16_t>(origin & 0xFFFF);
        registers.pbr = static_cast<std::uint8_t>(origin >> 16);
        registers.s = 0x01FF;
        registers.emulation = !native;
        registers.p = native ? 0x00 : 0x34;
        cpu.setRegisters(registers);
    }

    int step() { return cpu.step(); }
    [[nodiscard]] Cpu65816::Registers regs() const { return cpu.registers(); }
};

constexpr std::uint32_t kOrigin = 0x008000;

void test_register_widths() {
    // The same opcode reads one byte or two depending on M.
    {
        Run run{kOrigin, {0xA9, 0x34, 0x12}};  // LDA #$1234
        run.step();
        TW_CHECK_EQ(run.regs().c, 0x1234);
        TW_CHECK_EQ(run.regs().pc, 0x8003);
    }
    {
        Run run{kOrigin, {0xA9, 0x34, 0x12}};
        run.cpu.setRegisters([&] {
            auto registers = run.regs();
            registers.p = Cpu65816::kMemory8;
            return registers;
        }());
        run.step();
        TW_CHECK_EQ(run.regs().c, 0x0034);
        TW_CHECK_EQ(run.regs().pc, 0x8002);
    }
    // X is chosen by its own bit, so `LDX #$1234` reads two bytes even when the
    // accumulator is eight bits wide.
    {
        Run run{kOrigin, {0xA2, 0x34, 0x12}};
        run.cpu.setRegisters([&] {
            auto registers = run.regs();
            registers.p = Cpu65816::kMemory8;
            return registers;
        }());
        run.step();
        TW_CHECK_EQ(run.regs().x, 0x1234);
    }
    // Switching the index to eight bits clears the high bytes.
    {
        Run run{kOrigin, {0xA2, 0x34, 0x12, 0xE2, 0x10}};  // LDX #$1234 : SEP #$10
        run.step();
        run.step();
        TW_CHECK_EQ(run.regs().x, 0x0034);
        TW_CHECK_EQ(run.regs().p & Cpu65816::kIndex8, Cpu65816::kIndex8);
    }
}

void test_emulation() {
    // Emulation mode is the 6502: the registers are eight bits, the stack is in
    // page one, and the direct page is zero however it was set.
    Run run{0x008000, {0x38, 0xFB}};  // SEC : XCE
    run.cpu.setRegisters([&] {
        auto registers = run.regs();
        registers.emulation = true;
        registers.p = 0x34;
        registers.s = 0x01FF;
        registers.c = 0x1234;
        registers.d = 0x4321;
        return registers;
    }());
    run.step();
    run.step();
    TW_CHECK(run.regs().emulation);
    TW_CHECK_EQ(run.regs().p & Cpu65816::kMemory8, Cpu65816::kMemory8);
    TW_CHECK_EQ(run.regs().d, 0x0000);
    // Leaving emulation is one instruction, and the carry records where the
    // machine was: it ends up set, because emulation was on.
    Run leave{0x008000, {0x18, 0xFB}};  // CLC : XCE
    leave.cpu.setRegisters([&] {
        auto registers = leave.regs();
        registers.emulation = true;
        registers.p = 0x34;
        return registers;
    }());
    leave.step();
    leave.step();
    TW_CHECK(!leave.regs().emulation);
    TW_CHECK_EQ(leave.regs().p & Cpu65816::kCarry, Cpu65816::kCarry);
}

void test_banks() {
    // An absolute address points into the data bank, a long one names its own.
    Run run{kOrigin, {0x8D, 0x00, 0x20, 0x8F, 0x00, 0x21, 0x7E}};
    run.cpu.setRegisters([&] {
        auto registers = run.regs();
        registers.c = 0xABCD;
        registers.dbr = 0x05;
        return registers;
    }());
    run.step();
    run.step();
    TW_CHECK_EQ(run.bus.peek(0x052000), 0xCD);
    TW_CHECK_EQ(run.bus.peek(0x052001), 0xAB);
    TW_CHECK_EQ(run.bus.peek(0x7E2100), 0xCD);
    TW_CHECK_EQ(run.bus.peek(0x7E2101), 0xAB);
}

void test_direct_page() {
    // A direct page that is not page zero is reachable, costs a cycle, and
    // indexes into itself.
    Run run{kOrigin, {0xB5, 0x10}};  // LDA $10,X
    run.bus.poke(0x0214, 0x34);
    run.bus.poke(0x0215, 0x12);
    run.cpu.setRegisters([&] {
        auto registers = run.regs();
        registers.d = 0x0200;
        registers.x = 0x04;
        registers.p = Cpu65816::kMemory8;
        return registers;
    }());
    const int cycles = run.step();
    TW_CHECK_EQ(run.regs().c, 0x0034);
    // Four for the instruction, plus one because the index is sixteen bits
    // wide: a wide index always pays for the address it might need.
    TW_CHECK_EQ(cycles, 5);
    // A direct page that starts mid-page costs a cycle on every access.
    Run straddle{kOrigin, {0xB5, 0x10}};
    straddle.bus.poke(0x0315, 0x34);
    straddle.cpu.setRegisters([&] {
        auto registers = straddle.regs();
        registers.d = 0x0201;
        registers.x = 0x04;
        registers.p = Cpu65816::kMemory8;
        return registers;
    }());
    // Five for the wide index, plus one for the straddling direct page.
    TW_CHECK_EQ(straddle.step(), 6);
}

void test_new_address_modes() {
    // [dp] reads a three-byte pointer, so a store through it reaches a bank.
    {
        Run run{kOrigin, {0x87, 0x10}};  // STA [$10]
        run.bus.poke(0x0010, 0x00);
        run.bus.poke(0x0011, 0x21);
        run.bus.poke(0x0012, 0x7E);
        run.cpu.setRegisters([&] {
            auto registers = run.regs();
            registers.c = 0x0055;
            registers.p = Cpu65816::kMemory8;
            return registers;
        }());
        run.step();
        TW_CHECK_EQ(run.bus.peek(0x7E2100), 0x55);
    }
    // A stack-relative operand addresses the stack wherever it is.
    {
        Run run{kOrigin, {0xA3, 0x02}};  // LDA $02,S
        run.bus.poke(0x0201, 0x77);
        run.step();
        TW_CHECK_EQ(run.regs().c, 0x0077);
    }
    // (dp,S),Y goes through a pointer on the stack.
    {
        Run run{kOrigin, {0xB3, 0x02}};  // LDA ($02,S),Y
        run.bus.poke(0x0201, 0x00);
        run.bus.poke(0x0202, 0x20);
        run.bus.poke(0x002003, 0x9A);
        run.cpu.setRegisters([&] {
            auto registers = run.regs();
            registers.y = 0x03;
            registers.p = Cpu65816::kMemory8;
            return registers;
        }());
        run.step();
        TW_CHECK_EQ(run.regs().c, 0x009A);
    }
    // A long,X operand carries the index into the bank.
    {
        Run run{kOrigin, {0xBF, 0xFF, 0xFF, 0x7E}};  // LDA $7EFFFF,X
        run.bus.poke(0x7F0001, 0x42);
        run.cpu.setRegisters([&] {
            auto registers = run.regs();
            registers.x = 0x0002;
            registers.p = Cpu65816::kMemory8;
            return registers;
        }());
        run.step();
        TW_CHECK_EQ(run.regs().c, 0x0042);
    }
}

void test_arithmetic() {
    // Sixteen-bit addition and its carry.
    {
        Run run{kOrigin, {0x69, 0xFF, 0xFF}};  // ADC #$FFFF
        run.cpu.setRegisters([&] {
            auto registers = run.regs();
            registers.c = 0x0001;
            return registers;
        }());
        run.step();
        TW_CHECK_EQ(run.regs().c, 0x0000);
        TW_CHECK_EQ(run.regs().p & Cpu65816::kCarry, Cpu65816::kCarry);
        TW_CHECK_EQ(run.regs().p & Cpu65816::kZero, Cpu65816::kZero);
    }
    // Decimal mode adds a digit at a time, sixteen bits wide.
    {
        Run run{kOrigin, {0x69, 0x55, 0x55}};  // ADC #$5555
        run.cpu.setRegisters([&] {
            auto registers = run.regs();
            registers.c = 0x5555;
            registers.p = Cpu65816::kDecimal;
            return registers;
        }());
        run.step();
        TW_CHECK_EQ(run.regs().c, 0x1110);
        TW_CHECK_EQ(run.regs().p & Cpu65816::kCarry, Cpu65816::kCarry);
    }
    // Decimal subtraction borrows a digit at a time.
    {
        Run run{kOrigin, {0xE9, 0x01, 0x00}};  // SBC #$0001
        run.cpu.setRegisters([&] {
            auto registers = run.regs();
            registers.c = 0x0042;
            registers.p = Cpu65816::kDecimal | Cpu65816::kCarry;
            return registers;
        }());
        run.step();
        TW_CHECK_EQ(run.regs().c, 0x0041);
    }
    // Eight-bit decimal, the mode the old code paths use.
    {
        Run run{kOrigin, {0x69, 0x01}};  // ADC #$01
        run.cpu.setRegisters([&] {
            auto registers = run.regs();
            registers.c = 0x0009;
            registers.p = Cpu65816::kDecimal | Cpu65816::kMemory8;
            return registers;
        }());
        run.step();
        TW_CHECK_EQ(run.regs().c & 0xFF, 0x10);
    }
    // The overflow flag is about signed arithmetic.
    {
        Run run{kOrigin, {0x69, 0x01}};  // ADC #$01
        run.cpu.setRegisters([&] {
            auto registers = run.regs();
            registers.c = 0x007F;
            registers.p = Cpu65816::kMemory8;
            return registers;
        }());
        run.step();
        TW_CHECK_EQ(run.regs().p & Cpu65816::kOverflow, Cpu65816::kOverflow);
    }
}

void test_stack_and_transfers() {
    // A sixteen-bit push puts the high byte away first.
    {
        Run run{kOrigin, {0xF4, 0x34, 0x12, 0x68}};  // PEA $1234 : PLA
        run.step();
        run.step();
        TW_CHECK_EQ(run.regs().c, 0x1234);
        TW_CHECK_EQ(run.regs().s, 0x01FF);
    }
    // XBA swaps the halves of the accumulator.
    {
        Run run{kOrigin, {0xEB}};
        run.cpu.setRegisters([&] {
            auto registers = run.regs();
            registers.c = 0x1234;
            return registers;
        }());
        run.step();
        TW_CHECK_EQ(run.regs().c, 0x3412);
    }
    // A long call saves the program bank, and returning restores it.
    {
        Run run{0x008000, {0x22, 0x00, 0x90, 0x02}};  // JSL $029000
        run.step();
        TW_CHECK_EQ(run.regs().pbr, 0x02);
        TW_CHECK_EQ(run.regs().pc, 0x9000);
        run.bus.poke(0x029000, 0x6B);  // RTL
        run.step();
        TW_CHECK_EQ(run.regs().pbr, 0x00);
        TW_CHECK_EQ(run.regs().pc, 0x8004);
    }
}

void test_block_move() {
    // MVN copies C+1 bytes, walking both banks upward.
    Run run{kOrigin, {0x54, 0x7E, 0x7F}};  // MVN $7E,$7F
    run.bus.poke(0x7F1000, 0x11);
    run.bus.poke(0x7F1001, 0x22);
    run.bus.poke(0x7F1002, 0x33);
    run.cpu.setRegisters([&] {
        auto registers = run.regs();
        registers.c = 0x0002;  // three bytes
        registers.x = 0x1000;
        registers.y = 0x2000;
        return registers;
    }());
    run.step();
    TW_CHECK_EQ(run.bus.peek(0x7E2000), 0x11);
    TW_CHECK_EQ(run.bus.peek(0x7E2001), 0x22);
    TW_CHECK_EQ(run.bus.peek(0x7E2002), 0x33);
    TW_CHECK_EQ(run.regs().x, 0x1003);
    TW_CHECK_EQ(run.regs().y, 0x2003);
    TW_CHECK_EQ(run.regs().dbr, 0x7E);
}

void test_cycle_counts() {
    // The counts a frame budget is written against.
    {
        Run run{kOrigin, {0xEA}};
        TW_CHECK_EQ(run.step(), 2);  // NOP
    }
    {
        Run run{kOrigin, {0xAD, 0x34, 0x12}};
        TW_CHECK_EQ(run.step(), 4);  // LDA absolute
    }
    {
        // A page crossing costs a cycle for a read with an eight-bit index.
        Run run{kOrigin, {0xBD, 0xFF, 0x20}};  // LDA $20FF,X
        run.cpu.setRegisters([&] {
            auto registers = run.regs();
            registers.x = 0x02;
            registers.p = Cpu65816::kMemory8 | Cpu65816::kIndex8;
            return registers;
        }());
        TW_CHECK_EQ(run.step(), 5);
        // With a sixteen-bit index the extra cycle is always there.
        Run wide{kOrigin, {0xBD, 0xFF, 0x20}};
        wide.cpu.setRegisters([&] {
            auto registers = wide.regs();
            registers.x = 0x0002;
            registers.p = Cpu65816::kMemory8;
            return registers;
        }());
        TW_CHECK_EQ(wide.step(), 5);
    }
    {
        // A taken branch costs three, an untaken one two.
        Run taken{kOrigin, {0xD0, 0x02}};  // BNE +2
        taken.cpu.setRegisters([&] {
            auto registers = taken.regs();
            registers.p = 0;  // not equal
            return registers;
        }());
        TW_CHECK_EQ(taken.step(), 3);
        Run untaken{kOrigin, {0xD0, 0x02}};
        untaken.cpu.setRegisters([&] {
            auto registers = untaken.regs();
            registers.p = Cpu65816::kZero;  // equal, so BNE falls through
            return registers;
        }());
        TW_CHECK_EQ(untaken.step(), 2);
    }
}

void test_irq() {
    // An interrupt saves the program bank in native mode and vectors through
    // the native table.
    Run run{kOrigin, {0xEA}};
    run.bus.poke(0xFFEE, 0x00);
    run.bus.poke(0xFFEF, 0x90);
    run.cpu.setIrqLine(true);
    const int cycles = run.step();
    TW_CHECK_EQ(cycles, 8);
    TW_CHECK_EQ(run.regs().pc, 0x9000);
    TW_CHECK_EQ(run.regs().p & Cpu65816::kIrqDisable,
                Cpu65816::kIrqDisable);
}

}  // namespace

int main() {
    test_register_widths();
    test_emulation();
    test_banks();
    test_direct_page();
    test_new_address_modes();
    test_arithmetic();
    test_stack_and_transfers();
    test_block_move();
    test_cycle_counts();
    test_irq();
    return tw::test::summary("hb2_cpu_tests");
}
