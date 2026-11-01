// SPDX-License-Identifier: MIT
//
// The 6502 core: the documented instruction set, cycle-counted.
//
// The switch in step() is written in opcode order, one case per instruction, so
// the table can be read against a datasheet line by line. Three things are worth
// knowing before reading it:
//
//   * cycles. kBaseCycles holds the table's figure for each documented opcode.
//     Everything that depends on the operands is added where it happens: a page
//     crossing on an indexed read, a branch that is taken, a branch that is taken
//     across a page.
//   * flags. setZeroNegative is the only place Z and N are set from a result, and
//     the arithmetic helpers set C and V themselves, because their rules are not
//     the same for every instruction that touches them.
//   * decimal mode. ADC and SBC adjust a nibble at a time, and they report the
//     flags the chip reported: C from the decimal result, V from the binary one.
//
// An opcode the table does not have is counted and skipped rather than executed.
// The machine's ROMs never use one, and a test that finds a non-zero count has
// found a runaway rather than an instruction.

#include "kestrel/model1982/cpu6502.hpp"

namespace kestrel::model1982 {

namespace {

// Documented cycle counts, before the page-crossing and branch-taken
// penalties that depend on where the operands happen to be. A zero is an opcode
// this processor does not have: step() counts those and steps over one byte.
constexpr std::uint8_t kBaseCycles[256] = {
     7,  6,   0,   0,   0,  3,  5,   0,  3,  2,  2,   0,   0,  4,  6,   0,
     2,  5,   0,   0,   0,  4,  6,   0,  2,  4,   0,   0,   0,  4,  7,   0,
     6,  6,   0,   0,  3,  3,  5,   0,  4,  2,  2,   0,  4,  4,  6,   0,
     2,  5,   0,   0,   0,  4,  6,   0,  2,  4,   0,   0,   0,  4,  7,   0,
     6,  6,   0,   0,   0,  3,  5,   0,  3,  2,  2,   0,  3,  4,  6,   0,
     2,  5,   0,   0,   0,  4,  6,   0,  2,  4,   0,   0,   0,  4,  7,   0,
     6,  6,   0,   0,   0,  3,  5,   0,  4,  2,  2,   0,  5,  4,  6,   0,
     2,  5,   0,   0,   0,  4,  6,   0,  2,  4,   0,   0,   0,  4,  7,   0,
      0,  6,   0,   0,  3,  3,  3,   0,  2,   0,  2,   0,  4,  4,  4,   0,
     2,  6,   0,   0,  4,  4,  4,   0,  2,  5,  2,   0,   0,  5,   0,   0,
     2,  6,  2,   0,  3,  3,  3,   0,  2,  2,  2,   0,  4,  4,  4,   0,
     2,  5,   0,   0,  4,  4,  4,   0,  2,  4,  2,   0,  4,  4,  4,   0,
     2,  6,   0,   0,  3,  3,  5,   0,  2,  2,  2,   0,  4,  4,  6,   0,
     2,  5,   0,   0,   0,  4,  6,   0,  2,  4,   0,   0,   0,  4,  7,   0,
     2,  6,   0,   0,  3,  3,  5,   0,  2,  2,  2,   0,  4,  4,  6,   0,
     2,  5,   0,   0,   0,  4,  6,   0,  2,  4,   0,   0,   0,  4,  7,   0,
};

}  // namespace

// The state a real chip presents after a reset: the stack pointer has counted
// down by three, interrupts are masked, and decimal mode is off. Everything else
// is zero, which is why a ROM cannot rely on any register.
void Cpu6502::reset() {
    a_ = 0;
    x_ = 0;
    y_ = 0;
    sp_ = 0xFD;
    status_ = static_cast<std::uint8_t>(kInterruptDisable | kUnused);
    pc_ = readWord(0xFFFC);
    irqLine_ = false;
    nmiPending_ = false;
}

Cpu6502::Registers Cpu6502::registers() const {
    return {a_, x_, y_, sp_, status_, pc_};
}

void Cpu6502::setRegisters(const Registers& registers) {
    a_ = registers.a;
    x_ = registers.x;
    y_ = registers.y;
    sp_ = registers.sp;
    status_ = registers.status;
    pc_ = registers.pc;
}

std::uint8_t Cpu6502::fetch() {
    return bus_.read(pc_++);
}

std::uint16_t Cpu6502::readWord(std::uint16_t address) {
    const std::uint16_t low = bus_.read(address);
    const std::uint16_t high = bus_.read(static_cast<std::uint16_t>(address + 1));
    return static_cast<std::uint16_t>(low | (high << 8));
}

// The indirect jump's famous fault: both halves of the pointer come from the same
// page, so JMP ($02FF) reads its high byte from $0200. It is a bug on the chip,
// and a machine that emulated it away would not run the code written around it.
std::uint16_t Cpu6502::readWordBug(std::uint16_t address) {
    const std::uint16_t low = bus_.read(address);
    const std::uint16_t high =
        bus_.read(static_cast<std::uint16_t>((address & 0xFF00) | ((address + 1) & 0xFF)));
    return static_cast<std::uint16_t>(low | (high << 8));
}

std::uint16_t Cpu6502::fetchWord() {
    const std::uint16_t low = fetch();
    return static_cast<std::uint16_t>(low | (fetch() << 8));
}

Cpu6502::Effective Cpu6502::addressZeroPage() {
    return {fetch(), false};
}

Cpu6502::Effective Cpu6502::addressZeroPageX() {
    return {static_cast<std::uint8_t>(fetch() + x_), false};
}

Cpu6502::Effective Cpu6502::addressZeroPageY() {
    return {static_cast<std::uint8_t>(fetch() + y_), false};
}

Cpu6502::Effective Cpu6502::addressAbsolute() {
    return {fetchWord(), false};
}

Cpu6502::Effective Cpu6502::addressAbsoluteX() {
    const std::uint16_t base = fetchWord();
    const std::uint16_t address = static_cast<std::uint16_t>(base + x_);
    return {address, (base & 0xFF00) != (address & 0xFF00)};
}

Cpu6502::Effective Cpu6502::addressAbsoluteY() {
    const std::uint16_t base = fetchWord();
    const std::uint16_t address = static_cast<std::uint16_t>(base + y_);
    return {address, (base & 0xFF00) != (address & 0xFF00)};
}

Cpu6502::Effective Cpu6502::addressIndexedIndirect() {
    const std::uint8_t pointer = static_cast<std::uint8_t>(fetch() + x_);
    const std::uint16_t low = bus_.read(pointer);
    const std::uint16_t high = bus_.read(static_cast<std::uint8_t>(pointer + 1));
    return {static_cast<std::uint16_t>(low | (high << 8)), false};
}

Cpu6502::Effective Cpu6502::addressIndirectIndexed() {
    const std::uint8_t pointer = fetch();
    const std::uint16_t low = bus_.read(pointer);
    const std::uint16_t high = bus_.read(static_cast<std::uint8_t>(pointer + 1));
    const std::uint16_t base = static_cast<std::uint16_t>(low | (high << 8));
    const std::uint16_t address = static_cast<std::uint16_t>(base + y_);
    return {address, (base & 0xFF00) != (address & 0xFF00)};
}

// The stack lives in page one and wraps inside it: sixteen bits would be a memory
// the chip does not have.
void Cpu6502::push(std::uint8_t value) {
    bus_.write(static_cast<std::uint16_t>(0x0100 | sp_), value);
    --sp_;
}

std::uint8_t Cpu6502::pull() {
    ++sp_;
    return bus_.read(static_cast<std::uint16_t>(0x0100 | sp_));
}

// CMP, CPX and CPY: the subtraction that is only kept in the flags. Carry is set
// when the register is at least the operand, which is the borrow inverted.
void Cpu6502::compare(std::uint8_t reg, std::uint8_t value) {
    const int result = static_cast<int>(reg) - static_cast<int>(value);
    setFlag(kCarry, reg >= value);
    setZeroNegative(static_cast<std::uint8_t>(result & 0xFF));
}

// ADC. In binary the flags are the ones every reader expects. In decimal mode the
// two nibbles are adjusted separately, and the result is not the binary sum: Z is
// taken from the binary sum, N from the decimal digit that came out, and V from
// the binary signs, which is what the chip does and what a program that uses
// decimal mode twice in a row will notice.
void Cpu6502::adc(std::uint8_t value) {
    const int carry = flag(kCarry) ? 1 : 0;
    if (flag(kDecimal)) {
        int low = (a_ & 0x0F) + (value & 0x0F) + carry;
        if (low > 9) {
            low += 6;
        }
        int high = (a_ >> 4) + (value >> 4) + (low > 0x0F ? 1 : 0);
        setFlag(kZero, ((a_ + value + carry) & 0xFF) == 0);
        setFlag(kNegative, (high & 0x08) != 0);
        setFlag(kOverflow,
                (~(a_ ^ value) & (a_ ^ (high << 4)) & 0x80) != 0);
        if (high > 9) {
            high += 6;
        }
        setFlag(kCarry, high > 0x0F);
        a_ = static_cast<std::uint8_t>(((high << 4) | (low & 0x0F)) & 0xFF);
        return;
    }
    const int sum = static_cast<int>(a_) + static_cast<int>(value) + carry;
    setFlag(kCarry, sum > 0xFF);
    setFlag(kOverflow,
            (~(a_ ^ value) & (a_ ^ sum) & 0x80) != 0);
    a_ = static_cast<std::uint8_t>(sum & 0xFF);
    setZeroNegative(a_);
}

// SBC, which is ADC with the operand inverted, but is written out rather than
// composed: in decimal mode the borrow runs between the nibbles and a composed
// version gets the high digit wrong whenever the low one borrowed.
void Cpu6502::sbc(std::uint8_t value) {
    const int borrow = flag(kCarry) ? 0 : 1;
    if (flag(kDecimal)) {
        const int low = (a_ & 0x0F) - (value & 0x0F) - borrow;
        int high = (a_ >> 4) - (value >> 4);
        if (low < 0) {
            high -= 1;
        }
        if (high < 0) {
            high -= 6;
        }
        int adjustedLow = low & 0x0F;
        if (low < 0) {
            adjustedLow = (low - 6) & 0x0F;
        }
        const int difference = static_cast<int>(a_) - static_cast<int>(value) - borrow;
        setFlag(kCarry, difference >= 0);
        setFlag(kOverflow, ((a_ ^ value) & (a_ ^ difference) & 0x80) != 0);
        a_ = static_cast<std::uint8_t>(((high << 4) | adjustedLow) & 0xFF);
        setZeroNegative(a_);
        return;
    }
    const int difference = static_cast<int>(a_) - static_cast<int>(value) - borrow;
    setFlag(kCarry, difference >= 0);
    setFlag(kOverflow, ((a_ ^ value) & (a_ ^ difference) & 0x80) != 0);
    a_ = static_cast<std::uint8_t>(difference & 0xFF);
    setZeroNegative(a_);
}

std::uint8_t Cpu6502::asl(std::uint8_t value) {
    setFlag(kCarry, (value & 0x80) != 0);
    value = static_cast<std::uint8_t>(value << 1);
    setZeroNegative(value);
    return value;
}

std::uint8_t Cpu6502::lsr(std::uint8_t value) {
    setFlag(kCarry, (value & 0x01) != 0);
    value = static_cast<std::uint8_t>(value >> 1);
    setZeroNegative(value);
    return value;
}

std::uint8_t Cpu6502::rol(std::uint8_t value) {
    const bool carry = flag(kCarry);
    setFlag(kCarry, (value & 0x80) != 0);
    value = static_cast<std::uint8_t>((value << 1) | (carry ? 1 : 0));
    setZeroNegative(value);
    return value;
}

std::uint8_t Cpu6502::ror(std::uint8_t value) {
    const bool carry = flag(kCarry);
    setFlag(kCarry, (value & 0x01) != 0);
    value = static_cast<std::uint8_t>((value >> 1) | (carry ? 0x80 : 0));
    setZeroNegative(value);
    return value;
}

void Cpu6502::interrupt(std::uint16_t vector) {
    push(static_cast<std::uint8_t>(pc_ >> 8));
    push(static_cast<std::uint8_t>(pc_ & 0xFF));
    push(static_cast<std::uint8_t>((status_ & ~kBreak) | kUnused));
    setFlag(kInterruptDisable, true);
    pc_ = readWord(vector);
}

void Cpu6502::jam(std::uint8_t opcode) {
    ++illegalOpcodes_;
    lastIllegalOpcode_ = opcode;
}

// One instruction, or one interrupt if one is pending. An interrupt is taken
// before the opcode is fetched, which is the order the chip resolves them in, and
// costs the seven cycles of a BRK.
int Cpu6502::step() {
    if (nmiPending_) {
        nmiPending_ = false;
        interrupt(0xFFFA);
        ++instructions_;
        cyclesTotal_ += 7;
        return 7;
    }
    if (irqLine_ && !flag(kInterruptDisable)) {
        interrupt(0xFFFE);
        ++instructions_;
        cyclesTotal_ += 7;
        return 7;
    }
    const std::uint8_t opcode = fetch();
    int cycles = kBaseCycles[opcode];
    ++instructions_;

    switch (opcode) {
        case 0x69: {  // ADC #immediate
            const std::uint8_t value = fetch();
            adc(value);
        } break;
        case 0x65: {  // ADC zero page
            const Effective e = addressZeroPage();
            const std::uint8_t value = bus_.read(e.address);
            adc(value);
        } break;
        case 0x75: {  // ADC zero page,X
            const Effective e = addressZeroPageX();
            const std::uint8_t value = bus_.read(e.address);
            adc(value);
        } break;
        case 0x6D: {  // ADC absolute
            const Effective e = addressAbsolute();
            const std::uint8_t value = bus_.read(e.address);
            adc(value);
        } break;
        case 0x7D: {  // ADC absolute,X
            const Effective e = addressAbsoluteX();
            const std::uint8_t value = bus_.read(e.address);
            adc(value);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x79: {  // ADC absolute,Y
            const Effective e = addressAbsoluteY();
            const std::uint8_t value = bus_.read(e.address);
            adc(value);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x61: {  // ADC (indirect,X)
            const Effective e = addressIndexedIndirect();
            const std::uint8_t value = bus_.read(e.address);
            adc(value);
        } break;
        case 0x71: {  // ADC (indirect),Y
            const Effective e = addressIndirectIndexed();
            const std::uint8_t value = bus_.read(e.address);
            adc(value);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x29: {  // AND #immediate
            const std::uint8_t value = fetch();
            a_ &= value; setZeroNegative(a_);
        } break;
        case 0x25: {  // AND zero page
            const Effective e = addressZeroPage();
            const std::uint8_t value = bus_.read(e.address);
            a_ &= value; setZeroNegative(a_);
        } break;
        case 0x35: {  // AND zero page,X
            const Effective e = addressZeroPageX();
            const std::uint8_t value = bus_.read(e.address);
            a_ &= value; setZeroNegative(a_);
        } break;
        case 0x2D: {  // AND absolute
            const Effective e = addressAbsolute();
            const std::uint8_t value = bus_.read(e.address);
            a_ &= value; setZeroNegative(a_);
        } break;
        case 0x3D: {  // AND absolute,X
            const Effective e = addressAbsoluteX();
            const std::uint8_t value = bus_.read(e.address);
            a_ &= value; setZeroNegative(a_);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x39: {  // AND absolute,Y
            const Effective e = addressAbsoluteY();
            const std::uint8_t value = bus_.read(e.address);
            a_ &= value; setZeroNegative(a_);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x21: {  // AND (indirect,X)
            const Effective e = addressIndexedIndirect();
            const std::uint8_t value = bus_.read(e.address);
            a_ &= value; setZeroNegative(a_);
        } break;
        case 0x31: {  // AND (indirect),Y
            const Effective e = addressIndirectIndexed();
            const std::uint8_t value = bus_.read(e.address);
            a_ &= value; setZeroNegative(a_);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x0A: {  // ASL accumulator
            a_ = asl(a_);
        } break;
        case 0x06: {  // ASL zero page
            const Effective e = addressZeroPage();
            bus_.write(e.address, asl(bus_.read(e.address)));
        } break;
        case 0x16: {  // ASL zero page,X
            const Effective e = addressZeroPageX();
            bus_.write(e.address, asl(bus_.read(e.address)));
        } break;
        case 0x0E: {  // ASL absolute
            const Effective e = addressAbsolute();
            bus_.write(e.address, asl(bus_.read(e.address)));
        } break;
        case 0x1E: {  // ASL absolute,X
            const Effective e = addressAbsoluteX();
            bus_.write(e.address, asl(bus_.read(e.address)));
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x90: {  // BCC relative
            const std::int8_t offset = static_cast<std::int8_t>(fetch());
            if (flag(kCarry) == false) {
                const std::uint16_t from = pc_;
                pc_ = static_cast<std::uint16_t>(pc_ + offset);
                ++cycles;
                if ((from & 0xFF00) != (pc_ & 0xFF00)) {
                    ++cycles;
                }
            }
        } break;
        case 0xB0: {  // BCS relative
            const std::int8_t offset = static_cast<std::int8_t>(fetch());
            if (flag(kCarry) == true) {
                const std::uint16_t from = pc_;
                pc_ = static_cast<std::uint16_t>(pc_ + offset);
                ++cycles;
                if ((from & 0xFF00) != (pc_ & 0xFF00)) {
                    ++cycles;
                }
            }
        } break;
        case 0xF0: {  // BEQ relative
            const std::int8_t offset = static_cast<std::int8_t>(fetch());
            if (flag(kZero) == true) {
                const std::uint16_t from = pc_;
                pc_ = static_cast<std::uint16_t>(pc_ + offset);
                ++cycles;
                if ((from & 0xFF00) != (pc_ & 0xFF00)) {
                    ++cycles;
                }
            }
        } break;
        case 0x30: {  // BMI relative
            const std::int8_t offset = static_cast<std::int8_t>(fetch());
            if (flag(kNegative) == true) {
                const std::uint16_t from = pc_;
                pc_ = static_cast<std::uint16_t>(pc_ + offset);
                ++cycles;
                if ((from & 0xFF00) != (pc_ & 0xFF00)) {
                    ++cycles;
                }
            }
        } break;
        case 0xD0: {  // BNE relative
            const std::int8_t offset = static_cast<std::int8_t>(fetch());
            if (flag(kZero) == false) {
                const std::uint16_t from = pc_;
                pc_ = static_cast<std::uint16_t>(pc_ + offset);
                ++cycles;
                if ((from & 0xFF00) != (pc_ & 0xFF00)) {
                    ++cycles;
                }
            }
        } break;
        case 0x10: {  // BPL relative
            const std::int8_t offset = static_cast<std::int8_t>(fetch());
            if (flag(kNegative) == false) {
                const std::uint16_t from = pc_;
                pc_ = static_cast<std::uint16_t>(pc_ + offset);
                ++cycles;
                if ((from & 0xFF00) != (pc_ & 0xFF00)) {
                    ++cycles;
                }
            }
        } break;
        case 0x50: {  // BVC relative
            const std::int8_t offset = static_cast<std::int8_t>(fetch());
            if (flag(kOverflow) == false) {
                const std::uint16_t from = pc_;
                pc_ = static_cast<std::uint16_t>(pc_ + offset);
                ++cycles;
                if ((from & 0xFF00) != (pc_ & 0xFF00)) {
                    ++cycles;
                }
            }
        } break;
        case 0x70: {  // BVS relative
            const std::int8_t offset = static_cast<std::int8_t>(fetch());
            if (flag(kOverflow) == true) {
                const std::uint16_t from = pc_;
                pc_ = static_cast<std::uint16_t>(pc_ + offset);
                ++cycles;
                if ((from & 0xFF00) != (pc_ & 0xFF00)) {
                    ++cycles;
                }
            }
        } break;
        case 0x24: {  // BIT zero page
            const Effective e = addressZeroPage();
            const std::uint8_t value = bus_.read(e.address);
            setFlag(kZero, (a_ & value) == 0);
        setFlag(kNegative, (value & 0x80) != 0);
        setFlag(kOverflow, (value & 0x40) != 0);
        } break;
        case 0x2C: {  // BIT absolute
            const Effective e = addressAbsolute();
            const std::uint8_t value = bus_.read(e.address);
            setFlag(kZero, (a_ & value) == 0);
        setFlag(kNegative, (value & 0x80) != 0);
        setFlag(kOverflow, (value & 0x40) != 0);
        } break;
        case 0x00: {  // BRK implied
            // BRK
                // The break flag is set in the pushed copy only: the register the
                // program sees is not what the interrupt returns to.
                push(static_cast<std::uint8_t>((pc_ + 1) >> 8));
                push(static_cast<std::uint8_t>(pc_ + 1));
                push(static_cast<std::uint8_t>(status_ | kBreak | kUnused));
                setFlag(kInterruptDisable, true);
                pc_ = readWord(0xFFFE);
        } break;
        case 0x18: {  // CLC implied
        setFlag(kCarry, false);
        } break;
        case 0xD8: {  // CLD implied
        setFlag(kDecimal, false);
        } break;
        case 0x58: {  // CLI implied
        setFlag(kInterruptDisable, false);
        } break;
        case 0xB8: {  // CLV implied
        setFlag(kOverflow, false);
        } break;
        case 0xC9: {  // CMP #immediate
            const std::uint8_t value = fetch();
            compare(a_, value);
        } break;
        case 0xC5: {  // CMP zero page
            const Effective e = addressZeroPage();
            const std::uint8_t value = bus_.read(e.address);
            compare(a_, value);
        } break;
        case 0xD5: {  // CMP zero page,X
            const Effective e = addressZeroPageX();
            const std::uint8_t value = bus_.read(e.address);
            compare(a_, value);
        } break;
        case 0xCD: {  // CMP absolute
            const Effective e = addressAbsolute();
            const std::uint8_t value = bus_.read(e.address);
            compare(a_, value);
        } break;
        case 0xDD: {  // CMP absolute,X
            const Effective e = addressAbsoluteX();
            const std::uint8_t value = bus_.read(e.address);
            compare(a_, value);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0xD9: {  // CMP absolute,Y
            const Effective e = addressAbsoluteY();
            const std::uint8_t value = bus_.read(e.address);
            compare(a_, value);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0xC1: {  // CMP (indirect,X)
            const Effective e = addressIndexedIndirect();
            const std::uint8_t value = bus_.read(e.address);
            compare(a_, value);
        } break;
        case 0xD1: {  // CMP (indirect),Y
            const Effective e = addressIndirectIndexed();
            const std::uint8_t value = bus_.read(e.address);
            compare(a_, value);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0xE0: {  // CPX #immediate
            const std::uint8_t value = fetch();
            compare(x_, value);
        } break;
        case 0xE4: {  // CPX zero page
            const Effective e = addressZeroPage();
            const std::uint8_t value = bus_.read(e.address);
            compare(x_, value);
        } break;
        case 0xEC: {  // CPX absolute
            const Effective e = addressAbsolute();
            const std::uint8_t value = bus_.read(e.address);
            compare(x_, value);
        } break;
        case 0xC0: {  // CPY #immediate
            const std::uint8_t value = fetch();
            compare(y_, value);
        } break;
        case 0xC4: {  // CPY zero page
            const Effective e = addressZeroPage();
            const std::uint8_t value = bus_.read(e.address);
            compare(y_, value);
        } break;
        case 0xCC: {  // CPY absolute
            const Effective e = addressAbsolute();
            const std::uint8_t value = bus_.read(e.address);
            compare(y_, value);
        } break;
        case 0xC6: {  // DEC zero page
            const Effective e = addressZeroPage();
            bus_.write(e.address, static_cast<std::uint8_t>(bus_.read(e.address) - 1));
            setZeroNegative(bus_.read(e.address));
        } break;
        case 0xD6: {  // DEC zero page,X
            const Effective e = addressZeroPageX();
            bus_.write(e.address, static_cast<std::uint8_t>(bus_.read(e.address) - 1));
            setZeroNegative(bus_.read(e.address));
        } break;
        case 0xCE: {  // DEC absolute
            const Effective e = addressAbsolute();
            bus_.write(e.address, static_cast<std::uint8_t>(bus_.read(e.address) - 1));
            setZeroNegative(bus_.read(e.address));
        } break;
        case 0xDE: {  // DEC absolute,X
            const Effective e = addressAbsoluteX();
            bus_.write(e.address, static_cast<std::uint8_t>(bus_.read(e.address) - 1));
            setZeroNegative(bus_.read(e.address));
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0xCA: {  // DEX implied
        --x_;
                setZeroNegative(x_);
        } break;
        case 0x88: {  // DEY implied
        --y_;
                setZeroNegative(y_);
        } break;
        case 0x49: {  // EOR #immediate
            const std::uint8_t value = fetch();
            a_ ^= value; setZeroNegative(a_);
        } break;
        case 0x45: {  // EOR zero page
            const Effective e = addressZeroPage();
            const std::uint8_t value = bus_.read(e.address);
            a_ ^= value; setZeroNegative(a_);
        } break;
        case 0x55: {  // EOR zero page,X
            const Effective e = addressZeroPageX();
            const std::uint8_t value = bus_.read(e.address);
            a_ ^= value; setZeroNegative(a_);
        } break;
        case 0x4D: {  // EOR absolute
            const Effective e = addressAbsolute();
            const std::uint8_t value = bus_.read(e.address);
            a_ ^= value; setZeroNegative(a_);
        } break;
        case 0x5D: {  // EOR absolute,X
            const Effective e = addressAbsoluteX();
            const std::uint8_t value = bus_.read(e.address);
            a_ ^= value; setZeroNegative(a_);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x59: {  // EOR absolute,Y
            const Effective e = addressAbsoluteY();
            const std::uint8_t value = bus_.read(e.address);
            a_ ^= value; setZeroNegative(a_);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x41: {  // EOR (indirect,X)
            const Effective e = addressIndexedIndirect();
            const std::uint8_t value = bus_.read(e.address);
            a_ ^= value; setZeroNegative(a_);
        } break;
        case 0x51: {  // EOR (indirect),Y
            const Effective e = addressIndirectIndexed();
            const std::uint8_t value = bus_.read(e.address);
            a_ ^= value; setZeroNegative(a_);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0xE6: {  // INC zero page
            const Effective e = addressZeroPage();
            bus_.write(e.address, static_cast<std::uint8_t>(bus_.read(e.address) + 1));
            setZeroNegative(bus_.read(e.address));
        } break;
        case 0xF6: {  // INC zero page,X
            const Effective e = addressZeroPageX();
            bus_.write(e.address, static_cast<std::uint8_t>(bus_.read(e.address) + 1));
            setZeroNegative(bus_.read(e.address));
        } break;
        case 0xEE: {  // INC absolute
            const Effective e = addressAbsolute();
            bus_.write(e.address, static_cast<std::uint8_t>(bus_.read(e.address) + 1));
            setZeroNegative(bus_.read(e.address));
        } break;
        case 0xFE: {  // INC absolute,X
            const Effective e = addressAbsoluteX();
            bus_.write(e.address, static_cast<std::uint8_t>(bus_.read(e.address) + 1));
            setZeroNegative(bus_.read(e.address));
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0xE8: {  // INX implied
        ++x_;
                setZeroNegative(x_);
        } break;
        case 0xC8: {  // INY implied
        ++y_;
                setZeroNegative(y_);
        } break;
        case 0x4C: {  // JMP absolute
        pc_ = fetchWord();
        } break;
        case 0x6C: {  // JMP indirect
        pc_ = readWordBug(fetchWord());
        } break;
        case 0x20: {  // JSR absolute
            // JSR
                const std::uint8_t low = fetch();
                const std::uint8_t high = fetch();
                push(static_cast<std::uint8_t>((pc_ - 1) >> 8));
                push(static_cast<std::uint8_t>(pc_ - 1));
                pc_ = static_cast<std::uint16_t>(low | (high << 8));
        } break;
        case 0xA9: {  // LDA #immediate
            a_ = fetch();
            setZeroNegative(a_);
        } break;
        case 0xA5: {  // LDA zero page
            const Effective e = addressZeroPage();
            a_ = bus_.read(e.address);
            setZeroNegative(a_);
        } break;
        case 0xB5: {  // LDA zero page,X
            const Effective e = addressZeroPageX();
            a_ = bus_.read(e.address);
            setZeroNegative(a_);
        } break;
        case 0xAD: {  // LDA absolute
            const Effective e = addressAbsolute();
            a_ = bus_.read(e.address);
            setZeroNegative(a_);
        } break;
        case 0xBD: {  // LDA absolute,X
            const Effective e = addressAbsoluteX();
            a_ = bus_.read(e.address);
            setZeroNegative(a_);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0xB9: {  // LDA absolute,Y
            const Effective e = addressAbsoluteY();
            a_ = bus_.read(e.address);
            setZeroNegative(a_);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0xA1: {  // LDA (indirect,X)
            const Effective e = addressIndexedIndirect();
            a_ = bus_.read(e.address);
            setZeroNegative(a_);
        } break;
        case 0xB1: {  // LDA (indirect),Y
            const Effective e = addressIndirectIndexed();
            a_ = bus_.read(e.address);
            setZeroNegative(a_);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0xA2: {  // LDX #immediate
            x_ = fetch();
            setZeroNegative(x_);
        } break;
        case 0xA6: {  // LDX zero page
            const Effective e = addressZeroPage();
            x_ = bus_.read(e.address);
            setZeroNegative(x_);
        } break;
        case 0xB6: {  // LDX zero page,Y
            const Effective e = addressZeroPageY();
            x_ = bus_.read(e.address);
            setZeroNegative(x_);
        } break;
        case 0xAE: {  // LDX absolute
            const Effective e = addressAbsolute();
            x_ = bus_.read(e.address);
            setZeroNegative(x_);
        } break;
        case 0xBE: {  // LDX absolute,Y
            const Effective e = addressAbsoluteY();
            x_ = bus_.read(e.address);
            setZeroNegative(x_);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0xA0: {  // LDY #immediate
            y_ = fetch();
            setZeroNegative(y_);
        } break;
        case 0xA4: {  // LDY zero page
            const Effective e = addressZeroPage();
            y_ = bus_.read(e.address);
            setZeroNegative(y_);
        } break;
        case 0xB4: {  // LDY zero page,X
            const Effective e = addressZeroPageX();
            y_ = bus_.read(e.address);
            setZeroNegative(y_);
        } break;
        case 0xAC: {  // LDY absolute
            const Effective e = addressAbsolute();
            y_ = bus_.read(e.address);
            setZeroNegative(y_);
        } break;
        case 0xBC: {  // LDY absolute,X
            const Effective e = addressAbsoluteX();
            y_ = bus_.read(e.address);
            setZeroNegative(y_);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x4A: {  // LSR accumulator
            a_ = lsr(a_);
        } break;
        case 0x46: {  // LSR zero page
            const Effective e = addressZeroPage();
            bus_.write(e.address, lsr(bus_.read(e.address)));
        } break;
        case 0x56: {  // LSR zero page,X
            const Effective e = addressZeroPageX();
            bus_.write(e.address, lsr(bus_.read(e.address)));
        } break;
        case 0x4E: {  // LSR absolute
            const Effective e = addressAbsolute();
            bus_.write(e.address, lsr(bus_.read(e.address)));
        } break;
        case 0x5E: {  // LSR absolute,X
            const Effective e = addressAbsoluteX();
            bus_.write(e.address, lsr(bus_.read(e.address)));
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0xEA: {  // NOP implied
            // NOP
        } break;
        case 0x09: {  // ORA #immediate
            const std::uint8_t value = fetch();
            a_ |= value; setZeroNegative(a_);
        } break;
        case 0x05: {  // ORA zero page
            const Effective e = addressZeroPage();
            const std::uint8_t value = bus_.read(e.address);
            a_ |= value; setZeroNegative(a_);
        } break;
        case 0x15: {  // ORA zero page,X
            const Effective e = addressZeroPageX();
            const std::uint8_t value = bus_.read(e.address);
            a_ |= value; setZeroNegative(a_);
        } break;
        case 0x0D: {  // ORA absolute
            const Effective e = addressAbsolute();
            const std::uint8_t value = bus_.read(e.address);
            a_ |= value; setZeroNegative(a_);
        } break;
        case 0x1D: {  // ORA absolute,X
            const Effective e = addressAbsoluteX();
            const std::uint8_t value = bus_.read(e.address);
            a_ |= value; setZeroNegative(a_);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x19: {  // ORA absolute,Y
            const Effective e = addressAbsoluteY();
            const std::uint8_t value = bus_.read(e.address);
            a_ |= value; setZeroNegative(a_);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x01: {  // ORA (indirect,X)
            const Effective e = addressIndexedIndirect();
            const std::uint8_t value = bus_.read(e.address);
            a_ |= value; setZeroNegative(a_);
        } break;
        case 0x11: {  // ORA (indirect),Y
            const Effective e = addressIndirectIndexed();
            const std::uint8_t value = bus_.read(e.address);
            a_ |= value; setZeroNegative(a_);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x48: {  // PHA implied
        push(a_);
        } break;
        case 0x08: {  // PHP implied
        push(static_cast<std::uint8_t>(status_ | kBreak | kUnused));
        } break;
        case 0x68: {  // PLA implied
        a_ = pull();
                setZeroNegative(a_);
        } break;
        case 0x28: {  // PLP implied
        status_ = static_cast<std::uint8_t>((pull() & ~kBreak) | kUnused);
        } break;
        case 0x2A: {  // ROL accumulator
            a_ = rol(a_);
        } break;
        case 0x26: {  // ROL zero page
            const Effective e = addressZeroPage();
            bus_.write(e.address, rol(bus_.read(e.address)));
        } break;
        case 0x36: {  // ROL zero page,X
            const Effective e = addressZeroPageX();
            bus_.write(e.address, rol(bus_.read(e.address)));
        } break;
        case 0x2E: {  // ROL absolute
            const Effective e = addressAbsolute();
            bus_.write(e.address, rol(bus_.read(e.address)));
        } break;
        case 0x3E: {  // ROL absolute,X
            const Effective e = addressAbsoluteX();
            bus_.write(e.address, rol(bus_.read(e.address)));
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x6A: {  // ROR accumulator
            a_ = ror(a_);
        } break;
        case 0x66: {  // ROR zero page
            const Effective e = addressZeroPage();
            bus_.write(e.address, ror(bus_.read(e.address)));
        } break;
        case 0x76: {  // ROR zero page,X
            const Effective e = addressZeroPageX();
            bus_.write(e.address, ror(bus_.read(e.address)));
        } break;
        case 0x6E: {  // ROR absolute
            const Effective e = addressAbsolute();
            bus_.write(e.address, ror(bus_.read(e.address)));
        } break;
        case 0x7E: {  // ROR absolute,X
            const Effective e = addressAbsoluteX();
            bus_.write(e.address, ror(bus_.read(e.address)));
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x40: {  // RTI implied
            // RTI
                status_ = static_cast<std::uint8_t>((pull() & ~kBreak) | kUnused);
                pc_ = pull();
                pc_ = static_cast<std::uint16_t>(pc_ | (pull() << 8));
        } break;
        case 0x60: {  // RTS implied
            // RTS
                std::uint16_t address = pull();
                address = static_cast<std::uint16_t>(address | (pull() << 8));
                pc_ = static_cast<std::uint16_t>(address + 1);
        } break;
        case 0xE9: {  // SBC #immediate
            const std::uint8_t value = fetch();
            sbc(value);
        } break;
        case 0xE5: {  // SBC zero page
            const Effective e = addressZeroPage();
            const std::uint8_t value = bus_.read(e.address);
            sbc(value);
        } break;
        case 0xF5: {  // SBC zero page,X
            const Effective e = addressZeroPageX();
            const std::uint8_t value = bus_.read(e.address);
            sbc(value);
        } break;
        case 0xED: {  // SBC absolute
            const Effective e = addressAbsolute();
            const std::uint8_t value = bus_.read(e.address);
            sbc(value);
        } break;
        case 0xFD: {  // SBC absolute,X
            const Effective e = addressAbsoluteX();
            const std::uint8_t value = bus_.read(e.address);
            sbc(value);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0xF9: {  // SBC absolute,Y
            const Effective e = addressAbsoluteY();
            const std::uint8_t value = bus_.read(e.address);
            sbc(value);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0xE1: {  // SBC (indirect,X)
            const Effective e = addressIndexedIndirect();
            const std::uint8_t value = bus_.read(e.address);
            sbc(value);
        } break;
        case 0xF1: {  // SBC (indirect),Y
            const Effective e = addressIndirectIndexed();
            const std::uint8_t value = bus_.read(e.address);
            sbc(value);
            if (e.crossedPage) {
                ++cycles;
            }
        } break;
        case 0x38: {  // SEC implied
        setFlag(kCarry, true);
        } break;
        case 0xF8: {  // SED implied
        setFlag(kDecimal, true);
        } break;
        case 0x78: {  // SEI implied
        setFlag(kInterruptDisable, true);
        } break;
        case 0x85: {  // STA zero page
            const Effective e = addressZeroPage();
            bus_.write(e.address, a_);
        } break;
        case 0x95: {  // STA zero page,X
            const Effective e = addressZeroPageX();
            bus_.write(e.address, a_);
        } break;
        case 0x8D: {  // STA absolute
            const Effective e = addressAbsolute();
            bus_.write(e.address, a_);
        } break;
        case 0x9D: {  // STA absolute,X
            const Effective e = addressAbsoluteX();
            bus_.write(e.address, a_);
        } break;
        case 0x99: {  // STA absolute,Y
            const Effective e = addressAbsoluteY();
            bus_.write(e.address, a_);
        } break;
        case 0x81: {  // STA (indirect,X)
            const Effective e = addressIndexedIndirect();
            bus_.write(e.address, a_);
        } break;
        case 0x91: {  // STA (indirect),Y
            const Effective e = addressIndirectIndexed();
            bus_.write(e.address, a_);
        } break;
        case 0x86: {  // STX zero page
            const Effective e = addressZeroPage();
            bus_.write(e.address, x_);
        } break;
        case 0x96: {  // STX zero page,Y
            const Effective e = addressZeroPageY();
            bus_.write(e.address, x_);
        } break;
        case 0x8E: {  // STX absolute
            const Effective e = addressAbsolute();
            bus_.write(e.address, x_);
        } break;
        case 0x84: {  // STY zero page
            const Effective e = addressZeroPage();
            bus_.write(e.address, y_);
        } break;
        case 0x94: {  // STY zero page,X
            const Effective e = addressZeroPageX();
            bus_.write(e.address, y_);
        } break;
        case 0x8C: {  // STY absolute
            const Effective e = addressAbsolute();
            bus_.write(e.address, y_);
        } break;
        case 0xAA: {  // TAX implied
        x_ = a_;
                setZeroNegative(x_);
        } break;
        case 0xA8: {  // TAY implied
        y_ = a_;
                setZeroNegative(y_);
        } break;
        case 0xBA: {  // TSX implied
        x_ = sp_;
                setZeroNegative(x_);
        } break;
        case 0x8A: {  // TXA implied
        a_ = x_;
                setZeroNegative(a_);
        } break;
        case 0x9A: {  // TXS implied
        sp_ = x_;
        } break;
        case 0x98: {  // TYA implied
        a_ = y_;
                setZeroNegative(a_);
        } break;
        default:
            jam(opcode);
            break;
    }

    cyclesTotal_ += static_cast<std::uint64_t>(cycles);
    return cycles;
}

}  // namespace kestrel::model1982
