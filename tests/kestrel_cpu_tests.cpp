// SPDX-License-Identifier: MIT
//
// The 6502 core: addressing, arithmetic, flags, the stack, interrupts and the
// cycle counts the machine's real-time budget depends on.

#include "check.hpp"

#include "timewarp/kestrel/cpu6502.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace {

using timewarp::kestrel::Bus;
using timewarp::kestrel::Cpu6502;

/// 64K of plain memory: enough to run a program and read back what it did.
class RamBus final : public Bus {
public:
    RamBus() : memory_(0x10000, 0) {}

    [[nodiscard]] std::uint8_t read(std::uint16_t address) override {
        return memory_[address];
    }
    void write(std::uint16_t address, std::uint8_t value) override {
        memory_[address] = value;
    }

    void load(std::uint16_t address, std::initializer_list<std::uint8_t> bytes) {
        std::uint16_t at = address;
        for (std::uint8_t byte : bytes) {
            memory_[at++] = byte;
        }
    }
    void poke(std::uint16_t address, std::uint8_t value) {
        memory_[address] = value;
    }
    [[nodiscard]] std::uint8_t peek(std::uint16_t address) const {
        return memory_[address];
    }
    [[nodiscard]] std::uint16_t peekWord(std::uint16_t address) const {
        return static_cast<std::uint16_t>(memory_[address] |
                                          (memory_[address + 1] << 8));
    }

private:
    std::vector<std::uint8_t> memory_;
};

/// Runs one instruction from `opcode` and the bytes after it.
struct Run {
    RamBus bus;
    Cpu6502 cpu{bus};

    Run(std::uint16_t origin, std::initializer_list<std::uint8_t> program) {
        bus.load(origin, program);
        Cpu6502::Registers registers;
        registers.pc = origin;
        registers.sp = 0xFD;
        registers.status = 0x24;
        cpu.setRegisters(registers);
    }

    int step() { return cpu.step(); }
    [[nodiscard]] Cpu6502::Registers regs() const { return cpu.registers(); }
};

void test_addressing_modes() {
    {
        Run run{0x0200, {0xA9, 0x2A}};  // LDA #$2A
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x2A);
        TW_CHECK_EQ(int{run.regs().pc}, 0x0202);
    }
    {
        Run run{0x0200, {0xA5, 0x10}};  // LDA $10
        run.bus.poke(0x0010, 0x5A);
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x5A);
    }
    {
        Run run{0x0200, {0xB5, 0x10}};  // LDA $10,X
        run.bus.poke(0x0015, 0x77);
        Cpu6502::Registers registers = run.regs();
        registers.x = 5;
        run.cpu.setRegisters(registers);
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x77);
    }
    {
        Run run{0x0200, {0xB6, 0x10}};  // LDX $10,Y
        run.bus.poke(0x0013, 0x66);
        Cpu6502::Registers registers = run.regs();
        registers.y = 3;
        run.cpu.setRegisters(registers);
        run.step();
        TW_CHECK_EQ(int{run.regs().x}, 0x66);
    }
    {
        Run run{0x0200, {0xAD, 0x00, 0x40}};  // LDA $4000
        run.bus.poke(0x4000, 0x99);
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x99);
    }
    {
        Run run{0x0200, {0xBD, 0x00, 0x40}};  // LDA $4000,X
        run.bus.poke(0x4002, 0x88);
        Cpu6502::Registers registers = run.regs();
        registers.x = 2;
        run.cpu.setRegisters(registers);
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x88);
    }
    {
        Run run{0x0200, {0xB9, 0x00, 0x40}};  // LDA $4000,Y
        run.bus.poke(0x4003, 0x44);
        Cpu6502::Registers registers = run.regs();
        registers.y = 3;
        run.cpu.setRegisters(registers);
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x44);
    }
    {
        Run run{0x0200, {0xA1, 0x20}};  // LDA ($20,X)
        run.bus.poke(0x0020, 0x00);
        run.bus.poke(0x0021, 0x30);
        Cpu6502::Registers registers = run.regs();
        registers.x = 0;
        run.cpu.setRegisters(registers);
        run.bus.poke(0x3000, 0x11);
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x11);
    }
    {
        Run run{0x0200, {0xB1, 0x20}};  // LDA ($20),Y
        run.bus.poke(0x0020, 0xF0);
        run.bus.poke(0x0021, 0x30);
        run.bus.poke(0x30F1, 0x22);
        Cpu6502::Registers registers = run.regs();
        registers.y = 1;
        run.cpu.setRegisters(registers);
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x22);
    }
    {
        // The zero page pointer wraps: $FF reads high from $00, not $100.
        Run run{0x0200, {0xB1, 0xFF}};
        run.bus.poke(0x00FF, 0x00);
        run.bus.poke(0x0000, 0x40);
        run.bus.poke(0x0100, 0x99);  // would be the high byte on a broken core
        run.bus.poke(0x4000, 0xAB);
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0xAB);
    }
}

void test_arithmetic_and_flags() {
    {
        Run run{0x0200, {0xA9, 0x7F, 0x69, 0x01}};  // LDA #$7F; ADC #1
        run.step();
        run.step();
        const Cpu6502::Registers registers = run.regs();
        TW_CHECK_EQ(int{registers.a}, 0x80);
        TW_CHECK((registers.status & Cpu6502::kOverflow) != 0);
        TW_CHECK((registers.status & Cpu6502::kNegative) != 0);
        TW_CHECK((registers.status & Cpu6502::kCarry) == 0);
    }
    {
        Run run{0x0200, {0x18, 0xA9, 0xFF, 0x69, 0x02}};  // CLC; LDA #$FF; ADC #2
        run.step();
        run.step();
        run.step();
        const Cpu6502::Registers registers = run.regs();
        TW_CHECK_EQ(int{registers.a}, 0x01);
        TW_CHECK((registers.status & Cpu6502::kCarry) != 0);
        TW_CHECK((registers.status & Cpu6502::kZero) == 0);
    }
    {
        Run run{0x0200, {0x38, 0xA9, 0x00, 0xE9, 0x01}};  // SEC; LDA #0; SBC #1
        run.step();
        run.step();
        run.step();
        const Cpu6502::Registers registers = run.regs();
        TW_CHECK_EQ(int{registers.a}, 0xFF);
        TW_CHECK((registers.status & Cpu6502::kCarry) == 0);
    }
    {
        // Decimal arithmetic: $19 + $28 is $47, and the carry behaves.
        Run run{0x0200, {0xF8, 0x18, 0xA9, 0x19, 0x69, 0x28}};
        run.step();
        run.step();
        run.step();
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x47);
        TW_CHECK((run.regs().status & Cpu6502::kCarry) == 0);
    }
    {
        // $99 + $01 = $00 with a decimal carry.
        Run run{0x0200, {0xF8, 0x18, 0xA9, 0x99, 0x69, 0x01}};
        run.step();
        run.step();
        run.step();
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x00);
        TW_CHECK((run.regs().status & Cpu6502::kCarry) != 0);
    }
    {
        // Decimal subtract: $42 - $05 = $37 with no borrow.
        Run run{0x0200, {0xF8, 0x38, 0xA9, 0x42, 0xE9, 0x05}};
        run.step();
        run.step();
        run.step();
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x37);
    }
    {
        // Decimal borrow: $10 - $01 = $09, carry clear.
        Run run{0x0200, {0xF8, 0x38, 0xA9, 0x10, 0xE9, 0x01}};
        run.step();
        run.step();
        run.step();
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x09);
    }
    {
        Run run{0x0200, {0xA9, 0x10, 0xC9, 0x20}};  // LDA #$10; CMP #$20
        run.step();
        run.step();
        TW_CHECK((run.regs().status & Cpu6502::kCarry) == 0);
        TW_CHECK((run.regs().status & Cpu6502::kNegative) != 0);
    }
    {
        Run run{0x0200, {0xA9, 0x20, 0xC9, 0x20}};
        run.step();
        run.step();
        TW_CHECK((run.regs().status & Cpu6502::kCarry) != 0);
        TW_CHECK((run.regs().status & Cpu6502::kZero) != 0);
    }
}

void test_shift_rotate_and_memory() {
    {
        Run run{0x0200, {0xA9, 0x81, 0x0A}};  // LDA #$81; ASL A
        run.step();
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x02);
        TW_CHECK((run.regs().status & Cpu6502::kCarry) != 0);
    }
    {
        Run run{0x0200, {0xA9, 0x01, 0x4A}};  // LSR A
        run.step();
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x00);
        TW_CHECK((run.regs().status & Cpu6502::kCarry) != 0);
        TW_CHECK((run.regs().status & Cpu6502::kZero) != 0);
    }
    {
        Run run{0x0200, {0x38, 0xA9, 0x00, 0x2A}};  // SEC; LDA #0; ROL A
        run.step();
        run.step();
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x01);
    }
    {
        Run run{0x0200, {0x38, 0xA9, 0x00, 0x6A}};  // SEC; LDA #0; ROR A
        run.step();
        run.step();
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x80);
    }
    {
        Run run{0x0200, {0xA9, 0x05, 0x85, 0x10, 0xE6, 0x10, 0xC6, 0x10}};
        run.step();
        run.step();
        run.step();
        TW_CHECK_EQ(int{run.bus.peek(0x0010)}, 0x06);
        run.step();
        TW_CHECK_EQ(int{run.bus.peek(0x0010)}, 0x05);
    }
    {
        // BIT reads the top two bits of memory into N and V.
        Run run{0x0200, {0xA9, 0x00, 0x24, 0x10}};
        run.bus.poke(0x0010, 0xC0);
        run.step();
        run.step();
        const Cpu6502::Registers registers = run.regs();
        TW_CHECK((registers.status & Cpu6502::kNegative) != 0);
        TW_CHECK((registers.status & Cpu6502::kOverflow) != 0);
        TW_CHECK((registers.status & Cpu6502::kZero) != 0);
    }
}

void test_control_flow_and_stack() {
    {
        // JSR/RTS: the subroutine writes a byte and returns to the caller.
        RamBus bus;
        // $0200: JSR $0210 ; $0203: LDA #$01 ; $0205: BRK
        bus.load(0x0200, {0x20, 0x10, 0x02, 0xA9, 0x01, 0x00});
        // $0210: LDA #$42 ; STA $30 ; RTS
        bus.load(0x0210, {0xA9, 0x42, 0x85, 0x30, 0x60});
        Cpu6502 cpu{bus};
        Cpu6502::Registers registers;
        registers.pc = 0x0200;
        registers.sp = 0xFD;
        cpu.setRegisters(registers);

        cpu.step();  // JSR
        TW_CHECK_EQ(int{cpu.registers().pc}, 0x0210);
        cpu.step();
        cpu.step();
        cpu.step();  // RTS
        TW_CHECK_EQ(int{bus.peek(0x0030)}, 0x42);
        TW_CHECK_EQ(int{cpu.registers().pc}, 0x0203);
    }
    {
        // A backward branch loops; a taken branch costs an extra cycle.
        Run run{0x0200, {0xA2, 0x03, 0xCA, 0xD0, 0xFD}};  // LDX #3; DEX; BNE -3
        TW_CHECK_EQ(run.step(), 2);  // LDX #3
        TW_CHECK_EQ(run.step(), 2);  // DEX
        TW_CHECK_EQ(run.step(), 3);  // BNE, taken, no page cross
        TW_CHECK_EQ(run.step(), 2);  // DEX
        TW_CHECK_EQ(run.step(), 3);  // BNE, taken
        TW_CHECK_EQ(run.step(), 2);  // DEX -- X is now zero
        TW_CHECK_EQ(int{run.regs().x}, 0);
        const std::uint16_t pc = run.regs().pc;
        TW_CHECK_EQ(run.step(), 2);  // BNE, not taken
        TW_CHECK_EQ(int{run.regs().pc}, int{pc} + 2);
    }
    {
        // A taken branch that crosses a page costs two extra cycles.
        RamBus bus;
        bus.load(0x02FD, {0xD0, 0x01});  // BNE +1: $02FF to $0300, a page
        bus.load(0x0300, {0xEA, 0xEA});
        Cpu6502 cpu{bus};
        Cpu6502::Registers registers;
        registers.pc = 0x02FD;
        registers.status = Cpu6502::kUnused;  // Z clear, so the branch is taken
        cpu.setRegisters(registers);
        TW_CHECK_EQ(cpu.step(), 4);
        TW_CHECK_EQ(int{cpu.registers().pc}, 0x0300);
    }
    {
        // JMP (indirect) reproduces the page-boundary bug.
        RamBus bus;
        bus.load(0x0200, {0x6C, 0xFF, 0x30});
        bus.poke(0x30FF, 0x00);
        bus.poke(0x3100, 0x11);  // the correct high byte...
        bus.poke(0x3000, 0x40);  // ...ignored: the bug reads $3000 instead
        Cpu6502 cpu{bus};
        Cpu6502::Registers registers;
        registers.pc = 0x0200;
        cpu.setRegisters(registers);
        cpu.step();
        TW_CHECK_EQ(int{cpu.registers().pc}, 0x4000);
    }
    {
        // PHA/PLA round trip.
        Run run{0x0200, {0xA9, 0x7E, 0x48, 0xA9, 0x00, 0x68}};
        run.step();
        run.step();
        run.step();
        run.step();
        TW_CHECK_EQ(int{run.regs().a}, 0x7E);
    }
}

void test_interrupts() {
    {
        // IRQ: the handler is entered, the flags are pushed, RTI returns.
        RamBus bus;
        bus.load(0x0200, {0xEA, 0xEA});       // main program
        bus.load(0x0300, {0xA9, 0x55, 0x40});  // handler: LDA #$55; RTI
        bus.poke(0xFFFE, 0x00);
        bus.poke(0xFFFF, 0x03);
        Cpu6502 cpu{bus};
        Cpu6502::Registers registers;
        registers.pc = 0x0200;
        registers.status = Cpu6502::kUnused;  // interrupts enabled
        cpu.setRegisters(registers);

        cpu.setIrqLine(true);
        TW_CHECK_EQ(cpu.step(), 7);
        TW_CHECK_EQ(int{cpu.registers().pc}, 0x0300);
        TW_CHECK((cpu.registers().status & Cpu6502::kInterruptDisable) != 0);

        cpu.step();
        TW_CHECK_EQ(int{cpu.registers().a}, 0x55);
        cpu.step();  // RTI
        TW_CHECK_EQ(int{cpu.registers().pc}, 0x0200);
        TW_CHECK((cpu.registers().status & Cpu6502::kInterruptDisable) == 0);
    }
    {
        // With I set, a masked interrupt waits.
        RamBus bus;
        bus.load(0x0200, {0xEA});  // NOP
        bus.poke(0xFFFE, 0x00);
        bus.poke(0xFFFF, 0x03);
        Cpu6502 cpu{bus};
        Cpu6502::Registers registers;
        registers.pc = 0x0200;
        registers.status = static_cast<std::uint8_t>(Cpu6502::kUnused |
                                                     Cpu6502::kInterruptDisable);
        cpu.setRegisters(registers);
        cpu.setIrqLine(true);
        TW_CHECK_EQ(cpu.step(), 2);  // the NOP, not the interrupt
        TW_CHECK_EQ(int{cpu.registers().pc}, 0x0201);
    }
    {
        // NMI is edge-triggered and not maskable.
        RamBus bus;
        bus.load(0x0300, {0xA9, 0x22, 0x40});
        bus.poke(0xFFFA, 0x00);
        bus.poke(0xFFFB, 0x03);
        Cpu6502 cpu{bus};
        Cpu6502::Registers registers;
        registers.pc = 0x0200;
        registers.status = static_cast<std::uint8_t>(Cpu6502::kUnused |
                                                     Cpu6502::kInterruptDisable);
        cpu.setRegisters(registers);
        cpu.requestNmi();
        TW_CHECK_EQ(cpu.step(), 7);
        TW_CHECK_EQ(int{cpu.registers().pc}, 0x0300);
        cpu.step();
        cpu.step();
        TW_CHECK_EQ(int{cpu.registers().a}, 0x22);
        TW_CHECK_EQ(int{cpu.registers().pc}, 0x0200);
    }
    {
        // BRK vector, and the pushed status has the break bit set.
        RamBus bus;
        bus.load(0x0200, {0x00});
        bus.poke(0xFFFE, 0x00);
        bus.poke(0xFFFF, 0x03);
        Cpu6502 cpu{bus};
        Cpu6502::Registers registers;
        registers.pc = 0x0200;
        registers.status = Cpu6502::kUnused;
        cpu.setRegisters(registers);
        TW_CHECK_EQ(cpu.step(), 7);
        TW_CHECK_EQ(int{cpu.registers().pc}, 0x0300);
        // PC pushed is $0202 (the byte after BRK), high byte first.
        TW_CHECK_EQ(int{bus.peek(0x01FD)}, 0x02);
        TW_CHECK_EQ(int{bus.peek(0x01FC)}, 0x02);
        TW_CHECK((bus.peek(0x01FB) & Cpu6502::kBreak) != 0);
    }
}

void test_cycle_counts() {
    {
        RamBus bus;
        bus.load(0x0200, {0xA9, 0x01, 0x8D, 0x00, 0x40, 0xAD, 0x00, 0x40});
        Cpu6502 cpu{bus};
        Cpu6502::Registers registers;
        registers.pc = 0x0200;
        cpu.setRegisters(registers);
        TW_CHECK_EQ(cpu.step(), 2);  // LDA #
        TW_CHECK_EQ(cpu.step(), 4);  // STA abs
        TW_CHECK_EQ(cpu.step(), 4);  // LDA abs
        TW_CHECK_EQ(static_cast<int>(cpu.cycleCount()), 10);
        TW_CHECK_EQ(static_cast<int>(cpu.instructionCount()), 3);
    }
    {
        // LDA abs,X costs one more when the index crosses a page.
        RamBus bus;
        bus.load(0x0200, {0xBD, 0x00, 0x30});
        Cpu6502 cpu{bus};
        Cpu6502::Registers registers;
        registers.pc = 0x0200;
        registers.x = 0x10;
        cpu.setRegisters(registers);
        TW_CHECK_EQ(cpu.step(), 4);

        Cpu6502::Registers again;
        again.pc = 0x0200;
        again.x = 0x80;  // $3000 + $80 = $3080, still page 3
        cpu.setRegisters(again);
        TW_CHECK_EQ(cpu.step(), 4);

        again.pc = 0x0200;
        again.x = 0x90;  // $3090... still page 3: the operand is $3000
        cpu.setRegisters(again);
        TW_CHECK_EQ(cpu.step(), 4);

        // A store does not earn the penalty: it always takes its five cycles.
        bus.load(0x0210, {0x9D, 0x00, 0x30});
        Cpu6502::Registers store;
        store.pc = 0x0210;
        store.x = 0xF0;
        cpu.setRegisters(store);
        TW_CHECK_EQ(cpu.step(), 5);
    }
    {
        // A frame's worth of work: 20 000 cycles is the machine's tick.
        RamBus bus;
        bus.load(0x0200, {0xEA, 0x4C, 0x00, 0x02});  // NOP; JMP $0200
        Cpu6502 cpu{bus};
        Cpu6502::Registers registers;
        registers.pc = 0x0200;
        cpu.setRegisters(registers);
        while (cpu.cycleCount() < 20000) {
            cpu.step();
        }
        TW_CHECK(cpu.cycleCount() >= 20000);
        TW_CHECK_EQ(static_cast<int>(cpu.illegalOpcodes()), 0);
    }
}

}  // namespace

int main() {
    test_addressing_modes();
    test_arithmetic_and_flags();
    test_shift_rotate_and_memory();
    test_control_flow_and_stack();
    test_interrupts();
    test_cycle_counts();
    return tw::test::summary("kestrel_cpu_tests");
}
