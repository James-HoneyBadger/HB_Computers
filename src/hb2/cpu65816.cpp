// SPDX-License-Identifier: MIT
//
// The 65816 core. The opcode table is the processor's own map, one row per
// high nibble of the opcode, and it was generated from a single grid so that
// the operation enum, the addressing-mode enum and the table cannot disagree.

#include "timewarp/hb2/cpu65816.hpp"

namespace timewarp::hb2 {

namespace {

// What an instruction does, and how it finds its operand. The two lists and
// the table below were generated from one opcode map, so a name in the table
// always exists in its enum.
enum class Op : std::uint8_t {
    Adc, And, Asl, Bcc, Bcs, Beq, Bit, Bmi, Bne, Bpl, Bra, Brk, Brl, Bvc,
    Bvs, Clc, Cld, Cli, Clv, Cmp, Cop, Cpx, Cpy, Dec, Dex, Dey, Eor, Inc,
    Inx, Iny, Jml, Jmp, Jsl, Jsr, Lda, Ldx, Ldy, Lsr, Mvn, Mvp, Nop, Ora,
    Pea, Pei, Per, Pha, Phb, Phd, Phk, Php, Phx, Phy, Pla, Plb, Pld, Plp,
    Plx, Ply, Rep, Rol, Ror, Rti, Rtl, Rts, Sbc, Sec, Sed, Sei, Sep, Sta,
    Stp, Stx, Sty, Stz, Tax, Tay, Tcd, Tcs, Tdc, Trb, Tsb, Tsc, Tsx, Txa,
    Txs, Txy, Tya, Tyx, Wai, Wdm, Xba, Xce,
};

enum class Mode : std::uint8_t {
    Imp, Acc, ImmM, ImmX, Imm8, Imm16, Sig, Dp, DpX, DpY, DpInd, DpIndL,
    DpIndX, DpIndY, DpIndLY, SrS, SrSY, Abs, AbsX, AbsY, AbsInd, AbsIndL,
    AbsIndX, Long, LongX, Rel, Rel16, Blk,
};

struct Entry {
    Op op;
    Mode mode;
    std::uint8_t cycles;
};

constexpr Entry kOps[256] = {
    // $00
    {Op::Brk, Mode::Sig, 7}, {Op::Ora, Mode::DpIndX, 6}, {Op::Cop, Mode::Sig, 7}, {Op::Ora, Mode::SrS, 4},
    {Op::Tsb, Mode::Dp, 5}, {Op::Ora, Mode::Dp, 3}, {Op::Asl, Mode::Dp, 5}, {Op::Ora, Mode::DpIndL, 6},
    {Op::Php, Mode::Imp, 3}, {Op::Ora, Mode::ImmM, 2}, {Op::Asl, Mode::Acc, 2}, {Op::Phd, Mode::Imp, 4},
    {Op::Tsb, Mode::Abs, 6}, {Op::Ora, Mode::Abs, 4}, {Op::Asl, Mode::Abs, 6}, {Op::Ora, Mode::Long, 5},
    // $10
    {Op::Bpl, Mode::Rel, 2}, {Op::Ora, Mode::DpIndY, 5}, {Op::Ora, Mode::DpInd, 5}, {Op::Ora, Mode::SrSY, 7},
    {Op::Trb, Mode::Dp, 5}, {Op::Ora, Mode::DpX, 4}, {Op::Asl, Mode::DpX, 6}, {Op::Ora, Mode::DpIndLY, 6},
    {Op::Clc, Mode::Imp, 2}, {Op::Ora, Mode::AbsY, 4}, {Op::Inc, Mode::Acc, 2}, {Op::Tcs, Mode::Imp, 2},
    {Op::Trb, Mode::Abs, 6}, {Op::Ora, Mode::AbsX, 4}, {Op::Asl, Mode::AbsX, 7}, {Op::Ora, Mode::LongX, 6},
    // $20
    {Op::Jsr, Mode::Abs, 6}, {Op::And, Mode::DpIndX, 6}, {Op::Jsl, Mode::Long, 8}, {Op::And, Mode::SrS, 4},
    {Op::Bit, Mode::Dp, 3}, {Op::And, Mode::Dp, 3}, {Op::Rol, Mode::Dp, 5}, {Op::And, Mode::DpIndL, 6},
    {Op::Plp, Mode::Imp, 4}, {Op::And, Mode::ImmM, 2}, {Op::Rol, Mode::Acc, 2}, {Op::Pld, Mode::Imp, 5},
    {Op::Bit, Mode::Abs, 4}, {Op::And, Mode::Abs, 4}, {Op::Rol, Mode::Abs, 6}, {Op::And, Mode::Long, 5},
    // $30
    {Op::Bmi, Mode::Rel, 2}, {Op::And, Mode::DpIndY, 5}, {Op::And, Mode::DpInd, 5}, {Op::And, Mode::SrSY, 7},
    {Op::Bit, Mode::DpX, 4}, {Op::And, Mode::DpX, 4}, {Op::Rol, Mode::DpX, 6}, {Op::And, Mode::DpIndLY, 6},
    {Op::Sec, Mode::Imp, 2}, {Op::And, Mode::AbsY, 4}, {Op::Dec, Mode::Acc, 2}, {Op::Tsc, Mode::Imp, 2},
    {Op::Bit, Mode::AbsX, 4}, {Op::And, Mode::AbsX, 4}, {Op::Rol, Mode::AbsX, 7}, {Op::And, Mode::LongX, 6},
    // $40
    {Op::Rti, Mode::Imp, 6}, {Op::Eor, Mode::DpIndX, 6}, {Op::Wdm, Mode::Sig, 2}, {Op::Eor, Mode::SrS, 4},
    {Op::Mvp, Mode::Blk, 7}, {Op::Eor, Mode::Dp, 3}, {Op::Lsr, Mode::Dp, 5}, {Op::Eor, Mode::DpIndL, 6},
    {Op::Pha, Mode::Imp, 3}, {Op::Eor, Mode::ImmM, 2}, {Op::Lsr, Mode::Acc, 2}, {Op::Phk, Mode::Imp, 3},
    {Op::Jmp, Mode::Abs, 3}, {Op::Eor, Mode::Abs, 4}, {Op::Lsr, Mode::Abs, 6}, {Op::Eor, Mode::Long, 5},
    // $50
    {Op::Bvc, Mode::Rel, 2}, {Op::Eor, Mode::DpIndY, 5}, {Op::Eor, Mode::DpInd, 5}, {Op::Eor, Mode::SrSY, 7},
    {Op::Mvn, Mode::Blk, 7}, {Op::Eor, Mode::DpX, 4}, {Op::Lsr, Mode::DpX, 6}, {Op::Eor, Mode::DpIndLY, 6},
    {Op::Cli, Mode::Imp, 2}, {Op::Eor, Mode::AbsY, 4}, {Op::Phy, Mode::Imp, 3}, {Op::Tcd, Mode::Imp, 2},
    {Op::Jml, Mode::Long, 5}, {Op::Eor, Mode::AbsX, 4}, {Op::Lsr, Mode::AbsX, 7}, {Op::Eor, Mode::LongX, 6},
    // $60
    {Op::Rts, Mode::Imp, 6}, {Op::Adc, Mode::DpIndX, 6}, {Op::Per, Mode::Rel16, 6}, {Op::Adc, Mode::SrS, 4},
    {Op::Stz, Mode::Dp, 3}, {Op::Adc, Mode::Dp, 3}, {Op::Ror, Mode::Dp, 5}, {Op::Adc, Mode::DpIndL, 6},
    {Op::Pla, Mode::Imp, 4}, {Op::Adc, Mode::ImmM, 2}, {Op::Ror, Mode::Acc, 2}, {Op::Rtl, Mode::Imp, 6},
    {Op::Jmp, Mode::AbsInd, 5}, {Op::Adc, Mode::Abs, 4}, {Op::Ror, Mode::Abs, 6}, {Op::Adc, Mode::Long, 5},
    // $70
    {Op::Bvs, Mode::Rel, 2}, {Op::Adc, Mode::DpIndY, 5}, {Op::Adc, Mode::DpInd, 5}, {Op::Adc, Mode::SrSY, 7},
    {Op::Stz, Mode::DpX, 4}, {Op::Adc, Mode::DpX, 4}, {Op::Ror, Mode::DpX, 6}, {Op::Adc, Mode::DpIndLY, 6},
    {Op::Sei, Mode::Imp, 2}, {Op::Adc, Mode::AbsY, 4}, {Op::Ply, Mode::Imp, 4}, {Op::Tdc, Mode::Imp, 2},
    {Op::Jmp, Mode::AbsIndX, 6}, {Op::Adc, Mode::AbsX, 4}, {Op::Ror, Mode::AbsX, 7}, {Op::Adc, Mode::LongX, 6},
    // $80
    {Op::Bra, Mode::Rel, 2}, {Op::Sta, Mode::DpIndX, 6}, {Op::Brl, Mode::Rel16, 4}, {Op::Sta, Mode::SrS, 4},
    {Op::Sty, Mode::Dp, 3}, {Op::Sta, Mode::Dp, 3}, {Op::Stx, Mode::Dp, 3}, {Op::Sta, Mode::DpIndL, 6},
    {Op::Dey, Mode::Imp, 2}, {Op::Bit, Mode::ImmM, 2}, {Op::Txa, Mode::Imp, 2}, {Op::Phb, Mode::Imp, 3},
    {Op::Sty, Mode::Abs, 4}, {Op::Sta, Mode::Abs, 4}, {Op::Stx, Mode::Abs, 4}, {Op::Sta, Mode::Long, 5},
    // $90
    {Op::Bcc, Mode::Rel, 2}, {Op::Sta, Mode::DpIndY, 6}, {Op::Sta, Mode::DpInd, 5}, {Op::Sta, Mode::SrSY, 7},
    {Op::Sty, Mode::DpX, 4}, {Op::Sta, Mode::DpX, 4}, {Op::Stx, Mode::DpY, 4}, {Op::Sta, Mode::DpIndLY, 6},
    {Op::Tya, Mode::Imp, 2}, {Op::Sta, Mode::AbsY, 5}, {Op::Txs, Mode::Imp, 2}, {Op::Txy, Mode::Imp, 2},
    {Op::Stz, Mode::Abs, 4}, {Op::Sta, Mode::AbsX, 5}, {Op::Stz, Mode::AbsX, 5}, {Op::Sta, Mode::LongX, 5},
    // $A0
    {Op::Ldy, Mode::ImmX, 2}, {Op::Lda, Mode::DpIndX, 6}, {Op::Ldx, Mode::ImmX, 2}, {Op::Lda, Mode::SrS, 4},
    {Op::Ldy, Mode::Dp, 3}, {Op::Lda, Mode::Dp, 3}, {Op::Ldx, Mode::Dp, 3}, {Op::Lda, Mode::DpIndL, 6},
    {Op::Tay, Mode::Imp, 2}, {Op::Lda, Mode::ImmM, 2}, {Op::Tax, Mode::Imp, 2}, {Op::Plb, Mode::Imp, 4},
    {Op::Ldy, Mode::Abs, 4}, {Op::Lda, Mode::Abs, 4}, {Op::Ldx, Mode::Abs, 4}, {Op::Lda, Mode::Long, 5},
    // $B0
    {Op::Bcs, Mode::Rel, 2}, {Op::Lda, Mode::DpIndY, 5}, {Op::Lda, Mode::DpInd, 5}, {Op::Lda, Mode::SrSY, 7},
    {Op::Ldy, Mode::DpX, 4}, {Op::Lda, Mode::DpX, 4}, {Op::Ldx, Mode::DpY, 4}, {Op::Lda, Mode::DpIndLY, 6},
    {Op::Clv, Mode::Imp, 2}, {Op::Lda, Mode::AbsY, 4}, {Op::Tsx, Mode::Imp, 2}, {Op::Tyx, Mode::Imp, 2},
    {Op::Ldy, Mode::AbsX, 4}, {Op::Lda, Mode::AbsX, 4}, {Op::Ldx, Mode::AbsY, 4}, {Op::Lda, Mode::LongX, 6},
    // $C0
    {Op::Cpy, Mode::ImmX, 2}, {Op::Cmp, Mode::DpIndX, 6}, {Op::Rep, Mode::Imm8, 3}, {Op::Cmp, Mode::SrS, 4},
    {Op::Cpy, Mode::Dp, 3}, {Op::Cmp, Mode::Dp, 3}, {Op::Dec, Mode::Dp, 5}, {Op::Cmp, Mode::DpIndL, 6},
    {Op::Iny, Mode::Imp, 2}, {Op::Cmp, Mode::ImmM, 2}, {Op::Dex, Mode::Imp, 2}, {Op::Wai, Mode::Imp, 3},
    {Op::Cpy, Mode::Abs, 4}, {Op::Cmp, Mode::Abs, 4}, {Op::Dec, Mode::Abs, 6}, {Op::Cmp, Mode::Long, 5},
    // $D0
    {Op::Bne, Mode::Rel, 2}, {Op::Cmp, Mode::DpIndY, 5}, {Op::Cmp, Mode::DpInd, 5}, {Op::Cmp, Mode::SrSY, 7},
    {Op::Pei, Mode::DpInd, 6}, {Op::Cmp, Mode::DpX, 4}, {Op::Dec, Mode::DpX, 6}, {Op::Cmp, Mode::DpIndLY, 6},
    {Op::Cld, Mode::Imp, 2}, {Op::Cmp, Mode::AbsY, 4}, {Op::Phx, Mode::Imp, 3}, {Op::Stp, Mode::Imp, 3},
    {Op::Jml, Mode::AbsIndL, 6}, {Op::Cmp, Mode::AbsX, 4}, {Op::Dec, Mode::AbsX, 7}, {Op::Cmp, Mode::LongX, 6},
    // $E0
    {Op::Cpx, Mode::ImmX, 2}, {Op::Sbc, Mode::DpIndX, 6}, {Op::Sep, Mode::Imm8, 3}, {Op::Sbc, Mode::SrS, 4},
    {Op::Cpx, Mode::Dp, 3}, {Op::Sbc, Mode::Dp, 3}, {Op::Inc, Mode::Dp, 5}, {Op::Sbc, Mode::DpIndL, 6},
    {Op::Inx, Mode::Imp, 2}, {Op::Sbc, Mode::ImmM, 2}, {Op::Nop, Mode::Imp, 2}, {Op::Xba, Mode::Imp, 3},
    {Op::Cpx, Mode::Abs, 4}, {Op::Sbc, Mode::Abs, 4}, {Op::Inc, Mode::Abs, 6}, {Op::Sbc, Mode::Long, 5},
    // $F0
    {Op::Beq, Mode::Rel, 2}, {Op::Sbc, Mode::DpIndY, 5}, {Op::Sbc, Mode::DpInd, 5}, {Op::Sbc, Mode::SrSY, 7},
    {Op::Pea, Mode::Imm16, 5}, {Op::Sbc, Mode::DpX, 4}, {Op::Inc, Mode::DpX, 6}, {Op::Sbc, Mode::DpIndLY, 6},
    {Op::Sed, Mode::Imp, 2}, {Op::Sbc, Mode::AbsY, 4}, {Op::Plx, Mode::Imp, 4}, {Op::Xce, Mode::Imp, 2},
    {Op::Jsr, Mode::AbsIndX, 8}, {Op::Sbc, Mode::AbsX, 4}, {Op::Inc, Mode::AbsX, 7}, {Op::Sbc, Mode::LongX, 6},
};

// Vectors. The native ones are four bytes wide and sit below the emulation
// ones, which are the 6502's own and are all the emulation mode can reach.
constexpr std::uint16_t kNativeCop = 0xFFE4;
constexpr std::uint16_t kNativeBrk = 0xFFE6;
constexpr std::uint16_t kNativeIrq = 0xFFEE;
constexpr std::uint16_t kNativeNmi = 0xFFEA;
constexpr std::uint16_t kEmulationIrqBrk = 0xFFFE;
constexpr std::uint16_t kEmulationNmi = 0xFFFA;

}  // namespace

void Cpu65816::reset() {
    c_ = 0;
    x_ = 0;
    y_ = 0;
    s_ = 0x01FF;
    d_ = 0;
    dbr_ = 0;
    pbr_ = 0;
    emulation_ = true;
    p_ = static_cast<std::uint8_t>(kIrqDisable | kIndex8 | kMemory8);
    pc_ = readPointer(0xFFFC);
    stopped_ = false;
    waiting_ = false;
    irqLine_ = false;
    nmiPending_ = false;
}

Cpu65816::Registers Cpu65816::registers() const {
    return {c_, x_, y_, s_, d_, pc_, dbr_, pbr_, p_, emulation_};
}

void Cpu65816::setRegisters(const Registers& registers) {
    c_ = registers.c;
    x_ = registers.x;
    y_ = registers.y;
    s_ = registers.s;
    d_ = registers.d;
    pc_ = registers.pc;
    dbr_ = registers.dbr;
    pbr_ = registers.pbr;
    p_ = registers.p;
    emulation_ = registers.emulation;
    if (emulation_) {
        p_ |= static_cast<std::uint8_t>(kIndex8 | kMemory8);
        s_ = static_cast<std::uint16_t>(0x0100 | (s_ & 0xFF));
        x_ &= 0x00FF;
        y_ &= 0x00FF;
        d_ = 0;
    }
}

std::uint16_t Cpu65816::read16(std::uint32_t address) {
    const std::uint16_t low = read8(address);
    const std::uint16_t high = read8(address + 1);
    return static_cast<std::uint16_t>(low | (high << 8));
}

void Cpu65816::write16(std::uint32_t address, std::uint16_t value) {
    write8(address, static_cast<std::uint8_t>(value & 0xFF));
    write8(address + 1, static_cast<std::uint8_t>(value >> 8));
}

std::uint16_t Cpu65816::readPointer(std::uint16_t offset) {
    // A pointer never leaves bank zero, and its high byte wraps inside the
    // bank: $FFFF reads $FFFF and $0000.
    const std::uint16_t low = read8(offset);
    const std::uint16_t high = read8(static_cast<std::uint16_t>(offset + 1));
    return static_cast<std::uint16_t>(low | (high << 8));
}

std::uint8_t Cpu65816::fetchByte() {
    return read8(codeAddress(pc_++));
}

std::uint16_t Cpu65816::fetchWord() {
    const std::uint16_t low = fetchByte();
    return static_cast<std::uint16_t>(low | (fetchByte() << 8));
}

std::uint16_t Cpu65816::fetchOperandData() {
    return m8() ? fetchByte() : fetchWord();
}

std::uint16_t Cpu65816::fetchOperandIndex() {
    return x8() ? fetchByte() : fetchWord();
}

void Cpu65816::push8(std::uint8_t value) {
    write8(static_cast<std::uint32_t>(0x0100 | s_), value);
    s_ = static_cast<std::uint16_t>(s_ - 1);
}

std::uint8_t Cpu65816::pull8() {
    s_ = static_cast<std::uint16_t>(s_ + 1);
    return read8(static_cast<std::uint32_t>(0x0100 | s_));
}

void Cpu65816::push16(std::uint16_t value) {
    push8(static_cast<std::uint8_t>(value >> 8));
    push8(static_cast<std::uint8_t>(value & 0xFF));
}

std::uint16_t Cpu65816::pull16() {
    const std::uint16_t low = pull8();
    return static_cast<std::uint16_t>(low | (pull8() << 8));
}

std::uint16_t Cpu65816::readData(std::uint32_t address) {
    if (m8()) {
        return read8(address);
    }
    const std::uint8_t low = read8(address);
    const std::uint8_t high =
        read8((address & 0xFF0000) | ((address + 1) & 0xFFFF));
    return static_cast<std::uint16_t>(low | (high << 8));
}

void Cpu65816::writeData(std::uint32_t address, std::uint16_t value) {
    write8(address, static_cast<std::uint8_t>(value & 0xFF));
    if (!m8()) {
        write8((address & 0xFF0000) | ((address + 1) & 0xFFFF),
               static_cast<std::uint8_t>((value >> 8) & 0xFF));
    }
}

void Cpu65816::exchangeEmulation() {
    const bool carry = flag(kCarry);
    setFlag(kCarry, emulation_);
    emulation_ = carry;
    if (!emulation_) {
        return;
    }
    // Emulation mode is the 6502's: eight-bit registers, a stack page fixed at
    // $0100, a direct page fixed at zero, and banks zero.
    p_ |= static_cast<std::uint8_t>(kIndex8 | kMemory8);
    s_ = static_cast<std::uint16_t>(0x0100 | (s_ & 0xFF));
    x_ &= 0x00FF;
    y_ &= 0x00FF;
    d_ = 0;
    dbr_ = 0;
    pbr_ = 0;
}

void Cpu65816::interrupt(std::uint16_t vectorOffset, bool software) {
    waiting_ = false;
    if (emulation_) {
        push8(static_cast<std::uint8_t>(pc_ >> 8));
        push8(static_cast<std::uint8_t>(pc_ & 0xFF));
        push8(software ? static_cast<std::uint8_t>(p_ | 0x10)
                       : static_cast<std::uint8_t>(p_ & ~0x10));
        setFlag(kIrqDisable, true);
        setFlag(kDecimal, false);
        pbr_ = 0;
        pc_ = readPointer(vectorOffset);
        return;
    }
    push8(pbr_);
    push8(static_cast<std::uint8_t>(pc_ >> 8));
    push8(static_cast<std::uint8_t>(pc_ & 0xFF));
    push8(software ? p_ : static_cast<std::uint8_t>(p_ & ~0x10));
    setFlag(kIrqDisable, true);
    setFlag(kDecimal, false);
    pbr_ = 0;
    dbr_ = 0;
    pc_ = readPointer(vectorOffset);
}

void Cpu65816::adc(std::uint16_t value) {
    const std::uint16_t mask = m8() ? 0x00FF : 0xFFFF;
    const std::uint16_t a = static_cast<std::uint16_t>(c_ & mask);
    const std::uint16_t v = static_cast<std::uint16_t>(value & mask);
    const int carry = flag(kCarry) ? 1 : 0;
    const int binary = static_cast<int>(a) + static_cast<int>(v) + carry;
    const std::uint16_t result = static_cast<std::uint16_t>(binary & mask);
    const std::uint16_t sign = m8() ? 0x80 : 0x8000;

    setFlag(kCarry, binary > mask);
    setFlag(kOverflow, (~(a ^ v) & (a ^ binary) & sign) != 0);
    if (m8()) {
        setZeroNegative8(static_cast<std::uint8_t>(result));
    } else {
        setZeroNegative16(result);
    }

    std::uint16_t stored = result;
    if (flag(kDecimal)) {
        // One nibble at a time, which is what the decimal adjust does: add the
        // six whenever a digit passes nine, and carry into the next.
        unsigned adjusted = 0;
        unsigned carryIn = static_cast<unsigned>(carry);
        for (int shift = 0; shift < (m8() ? 8 : 16); shift += 4) {
            unsigned digit = ((a >> shift) & 0xF) + ((v >> shift) & 0xF) + carryIn;
            if (digit > 9) {
                digit += 6;
            }
            carryIn = (digit > 0xF) ? 1 : 0;
            adjusted |= (digit & 0xF) << shift;
        }
        setFlag(kCarry, carryIn != 0);
        stored = static_cast<std::uint16_t>(adjusted & mask);
    }
    if (m8()) {
        c_ = static_cast<std::uint16_t>((c_ & 0xFF00) | stored);
    } else {
        c_ = stored;
    }
}

void Cpu65816::sbc(std::uint16_t value) {
    const std::uint16_t mask = m8() ? 0x00FF : 0xFFFF;
    const std::uint16_t a = static_cast<std::uint16_t>(c_ & mask);
    const std::uint16_t v = static_cast<std::uint16_t>(value & mask);
    const int borrow = flag(kCarry) ? 0 : 1;
    const int binary = static_cast<int>(a) - static_cast<int>(v) - borrow;
    const std::uint16_t result = static_cast<std::uint16_t>(binary & mask);
    const std::uint16_t sign = m8() ? 0x80 : 0x8000;

    setFlag(kCarry, binary >= 0);
    setFlag(kOverflow, ((a ^ v) & (a ^ binary) & sign) != 0);
    if (m8()) {
        setZeroNegative8(static_cast<std::uint8_t>(result));
    } else {
        setZeroNegative16(result);
    }

    std::uint16_t stored = result;
    if (flag(kDecimal)) {
        unsigned adjusted = 0;
        int borrowIn = borrow;
        for (int shift = 0; shift < (m8() ? 8 : 16); shift += 4) {
            int digit = static_cast<int>((a >> shift) & 0xF) -
                        static_cast<int>((v >> shift) & 0xF) - borrowIn;
            if (digit < 0) {
                digit -= 6;
                borrowIn = 1;
            } else {
                borrowIn = 0;
            }
            adjusted |= static_cast<unsigned>(digit & 0xF) << shift;
        }
        setFlag(kCarry, borrowIn == 0);
        stored = static_cast<std::uint16_t>(adjusted & mask);
    }
    if (m8()) {
        c_ = static_cast<std::uint16_t>((c_ & 0xFF00) | stored);
    } else {
        c_ = stored;
    }
}

void Cpu65816::compare(std::uint16_t reg, std::uint16_t value) {
    if (m8()) {
        const int difference = static_cast<int>(reg & 0xFF) -
                               static_cast<int>(value & 0xFF);
        setFlag(kCarry, difference >= 0);
        setZeroNegative8(static_cast<std::uint8_t>(difference & 0xFF));
        return;
    }
    const int difference = static_cast<int>(reg) - static_cast<int>(value);
    setFlag(kCarry, difference >= 0);
    setZeroNegative16(static_cast<std::uint16_t>(difference & 0xFFFF));
}

std::uint16_t Cpu65816::aslValue(std::uint16_t value) {
    if (m8()) {
        setFlag(kCarry, (value & 0x80) != 0);
        const auto result = static_cast<std::uint8_t>(value << 1);
        setZeroNegative8(result);
        return result;
    }
    setFlag(kCarry, (value & 0x8000) != 0);
    const auto result = static_cast<std::uint16_t>(value << 1);
    setZeroNegative16(result);
    return result;
}

std::uint16_t Cpu65816::lsrValue(std::uint16_t value) {
    if (m8()) {
        setFlag(kCarry, (value & 0x01) != 0);
        const auto result = static_cast<std::uint8_t>(value >> 1);
        setZeroNegative8(result);
        return result;
    }
    setFlag(kCarry, (value & 0x0001) != 0);
    const auto result = static_cast<std::uint16_t>(value >> 1);
    setZeroNegative16(result);
    return result;
}

std::uint16_t Cpu65816::rolValue(std::uint16_t value) {
    const bool carry = flag(kCarry);
    if (m8()) {
        setFlag(kCarry, (value & 0x80) != 0);
        const auto result = static_cast<std::uint8_t>((value << 1) | (carry ? 1 : 0));
        setZeroNegative8(result);
        return result;
    }
    setFlag(kCarry, (value & 0x8000) != 0);
    const auto result =
        static_cast<std::uint16_t>((value << 1) | (carry ? 1 : 0));
    setZeroNegative16(result);
    return result;
}

std::uint16_t Cpu65816::rorValue(std::uint16_t value) {
    const bool carry = flag(kCarry);
    if (m8()) {
        setFlag(kCarry, (value & 0x01) != 0);
        const auto result =
            static_cast<std::uint8_t>((value >> 1) | (carry ? 0x80 : 0));
        setZeroNegative8(result);
        return result;
    }
    setFlag(kCarry, (value & 0x0001) != 0);
    const auto result =
        static_cast<std::uint16_t>((value >> 1) | (carry ? 0x8000 : 0));
    setZeroNegative16(result);
    return result;
}

int Cpu65816::step() {
    if (stopped_) {
        cyclesTotal_ += 1;
        return 1;
    }
    if (nmiPending_) {
        nmiPending_ = false;
        interrupt(emulation_ ? kEmulationNmi : kNativeNmi, false);
        ++instructions_;
        cyclesTotal_ += 8;
        return 8;
    }
    if (irqLine_ && !flag(kIrqDisable)) {
        interrupt(emulation_ ? kEmulationIrqBrk : kNativeIrq, false);
        ++instructions_;
        cyclesTotal_ += 8;
        return 8;
    }
    if (waiting_) {
        // `WAI` with no line to wake it: the processor idles a cycle at a time.
        cyclesTotal_ += 1;
        return 1;
    }

    const std::uint8_t opcode = fetchByte();
    const Entry& entry = kOps[opcode];
    int cycles = entry.cycles;
    ++instructions_;

    const auto indexValue = [this](std::uint16_t reg) {
        return x8() ? static_cast<std::uint16_t>(reg & 0xFF) : reg;
    };
    // A direct page whose low byte is not zero straddles a page boundary, and
    // every access through it costs a cycle.
    const auto directPage = [&](std::uint8_t offset) {
        if ((d_ & 0xFF) != 0) {
            ++cycles;
        }
        return static_cast<std::uint16_t>(d_ + offset);
    };
    const auto fetchLong = [this]() {
        const std::uint32_t low = fetchByte();
        const std::uint32_t middle = fetchByte();
        const std::uint32_t high = fetchByte();
        return low | (middle << 8) | (high << 16);
    };
    const auto readIndirectLong = [this](std::uint16_t slot) {
        const std::uint32_t low = readPointer(slot);
        return low | (static_cast<std::uint32_t>(read8(
                          static_cast<std::uint16_t>(slot + 2)))
                      << 16);
    };

    std::uint32_t ea = 0;
    std::uint16_t pointerSlot = 0;
    bool crossedPage = false;

    switch (entry.mode) {
    case Mode::Imp:
    case Mode::Acc:
    case Mode::ImmM:
    case Mode::ImmX:
    case Mode::Imm8:
    case Mode::Imm16:
    case Mode::Sig:
    case Mode::Rel:
    case Mode::Rel16:
    case Mode::Blk:
        break;
    case Mode::Dp:
        ea = directPage(fetchByte());
        break;
    case Mode::DpX:
    case Mode::DpY: {
        const std::uint8_t base = fetchByte();
        if ((d_ & 0xFF) != 0) {
            ++cycles;
        }
        const std::uint16_t index = indexValue(entry.mode == Mode::DpX ? x_ : y_);
        if ((d_ & 0xFF) == 0 && x8()) {
            ea = static_cast<std::uint16_t>(
                (d_ & 0xFF00) | static_cast<std::uint8_t>(base + index));
        } else {
            ea = static_cast<std::uint16_t>(d_ + base + index);
        }
        break;
    }
    case Mode::DpInd:
        pointerSlot = directPage(fetchByte());
        ea = dataAddress(readPointer(pointerSlot));
        break;
    case Mode::DpIndL:
        pointerSlot = directPage(fetchByte());
        ea = readIndirectLong(pointerSlot);
        break;
    case Mode::DpIndX:
        pointerSlot = directPage(static_cast<std::uint8_t>(fetchByte() +
                                                           (x_ & 0xFF)));
        ea = dataAddress(readPointer(pointerSlot));
        break;
    case Mode::DpIndY:
    case Mode::DpIndLY: {
        pointerSlot = directPage(fetchByte());
        const std::uint32_t pointer =
            entry.mode == Mode::DpIndY
                ? static_cast<std::uint32_t>(readPointer(pointerSlot))
                : readIndirectLong(pointerSlot);
        const std::uint32_t offset = pointer + indexValue(y_);
        if (entry.mode == Mode::DpIndY) {
            crossedPage =
                (pointer & 0xFF00) != (offset & 0xFF00);
            ea = dataAddress(static_cast<std::uint16_t>(offset & 0xFFFF));
        } else {
            ea = offset & 0xFFFFFF;
        }
        break;
    }
    case Mode::SrS:
        ea = static_cast<std::uint16_t>(s_ + fetchByte());
        break;
    case Mode::SrSY: {
        const std::uint16_t slot = static_cast<std::uint16_t>(s_ + fetchByte());
        const std::uint16_t base = readPointer(slot);
        const std::uint16_t offset =
            static_cast<std::uint16_t>(base + indexValue(y_));
        crossedPage = (base & 0xFF00) != (offset & 0xFF00);
        ea = dataAddress(offset);
        break;
    }
    case Mode::Abs:
        ea = dataAddress(fetchWord());
        break;
    case Mode::AbsX:
    case Mode::AbsY: {
        const std::uint16_t base = fetchWord();
        const std::uint16_t offset = static_cast<std::uint16_t>(
            base + indexValue(entry.mode == Mode::AbsX ? x_ : y_));
        crossedPage = (base & 0xFF00) != (offset & 0xFF00);
        ea = dataAddress(offset);
        break;
    }
    case Mode::AbsInd:
        ea = readPointer(fetchWord());
        break;
    case Mode::AbsIndX:
        ea = readPointer(static_cast<std::uint16_t>(
            fetchWord() + indexValue(x_)));
        break;
    case Mode::AbsIndL: {
        const std::uint16_t slot = fetchWord();
        ea = readIndirectLong(slot);
        break;
    }
    case Mode::Long:
        ea = fetchLong();
        break;
    case Mode::LongX:
        ea = (fetchLong() + indexValue(x_)) & 0xFFFFFF;
        break;
    }

    const auto operand = [&](Mode mode) -> std::uint16_t {
        return mode == Mode::ImmM ? fetchOperandData() : readData(ea);
    };
    const auto result = [this](std::uint16_t value) {
        if (m8()) {
            c_ = static_cast<std::uint16_t>((c_ & 0xFF00) | (value & 0xFF));
            setZeroNegative8(static_cast<std::uint8_t>(value & 0xFF));
        } else {
            c_ = value;
            setZeroNegative16(value);
        }
    };
    const auto storeX = [this](std::uint16_t value) {
        if (x8()) {
            x_ = static_cast<std::uint16_t>(value & 0xFF);
            setZeroNegative8(static_cast<std::uint8_t>(value & 0xFF));
        } else {
            x_ = value;
            setZeroNegative16(value);
        }
    };
    const auto storeY = [this](std::uint16_t value) {
        if (x8()) {
            y_ = static_cast<std::uint16_t>(value & 0xFF);
            setZeroNegative8(static_cast<std::uint8_t>(value & 0xFF));
        } else {
            y_ = value;
            setZeroNegative16(value);
        }
    };
    const std::uint16_t accMask = m8() ? 0x00FF : 0xFFFF;

    switch (entry.op) {
    case Op::Adc:
        adc(operand(entry.mode));
        break;
    case Op::Sbc:
        sbc(operand(entry.mode));
        break;
    case Op::And: {
        const std::uint16_t value = operand(entry.mode);
        result(static_cast<std::uint16_t>((c_ & accMask) & value));
        break;
    }
    case Op::Ora: {
        const std::uint16_t value = operand(entry.mode);
        result(static_cast<std::uint16_t>((c_ & accMask) | value));
        break;
    }
    case Op::Eor: {
        const std::uint16_t value = operand(entry.mode);
        result(static_cast<std::uint16_t>((c_ & accMask) ^ value));
        break;
    }
    case Op::Bit: {
        if (entry.mode == Mode::ImmM) {
            const std::uint16_t value = fetchOperandData();
            setFlag(kZero, ((c_ & value) & accMask) == 0);
        } else {
            const std::uint16_t value = readData(ea);
            setFlag(kZero, ((c_ & value) & accMask) == 0);
            if (m8()) {
                setFlag(kNegative, (value & 0x80) != 0);
                setFlag(kOverflow, (value & 0x40) != 0);
            } else {
                setFlag(kNegative, (value & 0x8000) != 0);
                setFlag(kOverflow, (value & 0x4000) != 0);
            }
        }
        break;
    }
    case Op::Cmp:
        compare(c_, operand(entry.mode));
        break;
    case Op::Cpx:
    case Op::Cpy: {
        const std::uint16_t reg = entry.op == Op::Cpx ? x_ : y_;
        const std::uint16_t value =
            entry.mode == Mode::ImmX ? fetchOperandIndex() : readData(ea);
        const std::uint16_t mask = x8() ? 0x00FF : 0xFFFF;
        const int difference = static_cast<int>(reg & mask) -
                               static_cast<int>(value & mask);
        setFlag(kCarry, difference >= 0);
        if (x8()) {
            setZeroNegative8(static_cast<std::uint8_t>(difference & 0xFF));
        } else {
            setZeroNegative16(static_cast<std::uint16_t>(difference & 0xFFFF));
        }
        break;
    }
    case Op::Asl:
    case Op::Lsr:
    case Op::Rol:
    case Op::Ror: {
        const bool accumulator = entry.mode == Mode::Acc;
        const std::uint16_t value = accumulator ? (c_ & accMask) : readData(ea);
        std::uint16_t shifted = 0;
        switch (entry.op) {
        case Op::Asl: shifted = aslValue(value); break;
        case Op::Lsr: shifted = lsrValue(value); break;
        case Op::Rol: shifted = rolValue(value); break;
        default: shifted = rorValue(value); break;
        }
        if (accumulator) {
            if (m8()) {
                c_ = static_cast<std::uint16_t>((c_ & 0xFF00) | shifted);
            } else {
                c_ = shifted;
            }
        } else {
            writeData(ea, shifted);
        }
        break;
    }
    case Op::Dec: {
        if (entry.mode == Mode::Acc) {
            const std::uint16_t value =
                static_cast<std::uint16_t>((c_ & accMask) - 1);
            result(value);
        } else {
            writeData(ea, static_cast<std::uint16_t>(readData(ea) - 1));
        }
        break;
    }
    case Op::Inc: {
        if (entry.mode == Mode::Acc) {
            const std::uint16_t value =
                static_cast<std::uint16_t>((c_ & accMask) + 1);
            result(value);
        } else {
            writeData(ea, static_cast<std::uint16_t>(readData(ea) + 1));
        }
        break;
    }
    case Op::Dex: storeX(static_cast<std::uint16_t>(x_ - 1)); break;
    case Op::Dey: storeY(static_cast<std::uint16_t>(y_ - 1)); break;
    case Op::Inx: storeX(static_cast<std::uint16_t>(x_ + 1)); break;
    case Op::Iny: storeY(static_cast<std::uint16_t>(y_ + 1)); break;
    case Op::Lda: result(operand(entry.mode)); break;
    case Op::Ldx:
        storeX(entry.mode == Mode::ImmX ? fetchOperandIndex() : readData(ea));
        break;
    case Op::Ldy:
        storeY(entry.mode == Mode::ImmX ? fetchOperandIndex() : readData(ea));
        break;
    case Op::Sta:
        writeData(ea, static_cast<std::uint16_t>(c_ & accMask));
        break;
    case Op::Stx: writeData(ea, x_); break;
    case Op::Sty: writeData(ea, y_); break;
    case Op::Stz: writeData(ea, 0); break;
    case Op::Tsb: {
        const std::uint16_t value = readData(ea);
        setFlag(kZero, ((value & c_) & accMask) == 0);
        writeData(ea, static_cast<std::uint16_t>(value | (c_ & accMask)));
        break;
    }
    case Op::Trb: {
        const std::uint16_t value = readData(ea);
        setFlag(kZero, ((value & c_) & accMask) == 0);
        writeData(ea,
                  static_cast<std::uint16_t>(value & ~(c_ & accMask)));
        break;
    }
    case Op::Bcc:
    case Op::Bcs:
    case Op::Beq:
    case Op::Bmi:
    case Op::Bne:
    case Op::Bpl:
    case Op::Bvc:
    case Op::Bvs:
    case Op::Bra: {
        const auto offset = static_cast<std::int8_t>(fetchByte());
        bool taken = false;
        switch (entry.op) {
        case Op::Bcc: taken = !flag(kCarry); break;
        case Op::Bcs: taken = flag(kCarry); break;
        case Op::Beq: taken = flag(kZero); break;
        case Op::Bmi: taken = flag(kNegative); break;
        case Op::Bne: taken = !flag(kZero); break;
        case Op::Bpl: taken = !flag(kNegative); break;
        case Op::Bvc: taken = !flag(kOverflow); break;
        case Op::Bvs: taken = flag(kOverflow); break;
        default: taken = true; break;
        }
        if (taken) {
            const std::uint16_t target =
                static_cast<std::uint16_t>(pc_ + offset);
            ++cycles;  // a taken branch is one cycle longer than an untaken one
            if ((target & 0xFF00) != (pc_ & 0xFF00)) {
                ++cycles;  // and one more when it leaves the page
            }
            pc_ = target;
        }
        break;
    }
    case Op::Brl: {
        const auto offset = static_cast<std::int16_t>(fetchWord());
        pc_ = static_cast<std::uint16_t>(pc_ + offset);
        break;
    }
    case Op::Brk:
        (void)fetchByte();  // the signature byte, which this machine ignores
        interrupt(emulation_ ? kEmulationIrqBrk : kNativeBrk, true);
        break;
    case Op::Cop:
        (void)fetchByte();
        interrupt(emulation_ ? kEmulationIrqBrk : kNativeCop, true);
        break;
    case Op::Wdm:
        (void)fetchByte();
        break;
    case Op::Clc: setFlag(kCarry, false); break;
    case Op::Sec: setFlag(kCarry, true); break;
    case Op::Cld: setFlag(kDecimal, false); break;
    case Op::Sed: setFlag(kDecimal, true); break;
    case Op::Cli: setFlag(kIrqDisable, false); break;
    case Op::Sei: setFlag(kIrqDisable, true); break;
    case Op::Clv: setFlag(kOverflow, false); break;
    case Op::Jmp:
    case Op::Jml:
        pbr_ = static_cast<std::uint8_t>(ea >> 16);
        pc_ = static_cast<std::uint16_t>(ea & 0xFFFF);
        break;
    case Op::Jsr:
        push16(static_cast<std::uint16_t>(pc_ - 1));
        pc_ = static_cast<std::uint16_t>(ea & 0xFFFF);
        break;
    case Op::Jsl:
        push8(pbr_);
        push16(static_cast<std::uint16_t>(pc_ - 1));
        pbr_ = static_cast<std::uint8_t>(ea >> 16);
        pc_ = static_cast<std::uint16_t>(ea & 0xFFFF);
        break;
    case Op::Rts:
        pc_ = static_cast<std::uint16_t>(pull16() + 1);
        break;
    case Op::Rtl:
        pc_ = static_cast<std::uint16_t>(pull16() + 1);
        pbr_ = pull8();
        break;
    case Op::Rti:
        p_ = pull8();
        if (emulation_) {
            p_ |= static_cast<std::uint8_t>(kIndex8 | kMemory8);
        }
        pc_ = pull16();
        if (!emulation_) {
            pbr_ = pull8();
        } else {
            pbr_ = 0;
        }
        if (x8()) {
            x_ &= 0x00FF;
            y_ &= 0x00FF;
        }
        break;
    case Op::Mvn:
    case Op::Mvp: {
        const std::uint8_t destination = fetchByte();
        const std::uint8_t source = fetchByte();
        const std::int32_t step = entry.op == Op::Mvn ? 1 : -1;
        const std::uint32_t count = static_cast<std::uint32_t>(
            (x8() ? (c_ & 0xFF) : c_)) + 1;
        std::uint32_t from =
            (static_cast<std::uint32_t>(source) << 16) | x_;
        std::uint32_t to =
            (static_cast<std::uint32_t>(destination) << 16) | y_;
        for (std::uint32_t i = 0; i < count; ++i) {
            write8(to, read8(from));
            from = (from & 0xFF0000) |
                   static_cast<std::uint32_t>(
                       static_cast<std::uint16_t>(from + step));
            to = (to & 0xFF0000) |
                 static_cast<std::uint32_t>(
                     static_cast<std::uint16_t>(to + step));
        }
        x_ = static_cast<std::uint16_t>(from & 0xFFFF);
        y_ = static_cast<std::uint16_t>(to & 0xFFFF);
        if (x8()) {
            x_ &= 0x00FF;
            y_ &= 0x00FF;
        }
        c_ = 0xFFFF;  // the count runs out, which is what the chip leaves
        dbr_ = destination;
        break;
    }
    case Op::Nop:
        break;
    case Op::Pea:
        push16(fetchWord());
        break;
    case Op::Pei:
        push16(readPointer(pointerSlot));
        break;
    case Op::Per: {
        const auto offset = static_cast<std::int16_t>(fetchWord());
        push16(static_cast<std::uint16_t>(pc_ + offset));
        break;
    }
    case Op::Pha:
        if (m8()) {
            push8(static_cast<std::uint8_t>(c_ & 0xFF));
        } else {
            push16(c_);
        }
        break;
    case Op::Pla:
        if (m8()) {
            c_ = static_cast<std::uint16_t>((c_ & 0xFF00) | pull8());
            setZeroNegative8(static_cast<std::uint8_t>(c_ & 0xFF));
        } else {
            c_ = pull16();
            setZeroNegative16(c_);
        }
        break;
    case Op::Phx:
        if (x8()) {
            push8(static_cast<std::uint8_t>(x_ & 0xFF));
        } else {
            push16(x_);
        }
        break;
    case Op::Plx:
        if (x8()) {
            x_ = pull8();
            setZeroNegative8(static_cast<std::uint8_t>(x_));
        } else {
            x_ = pull16();
            setZeroNegative16(x_);
        }
        break;
    case Op::Phy:
        if (x8()) {
            push8(static_cast<std::uint8_t>(y_ & 0xFF));
        } else {
            push16(y_);
        }
        break;
    case Op::Ply:
        if (x8()) {
            y_ = pull8();
            setZeroNegative8(static_cast<std::uint8_t>(y_));
        } else {
            y_ = pull16();
            setZeroNegative16(y_);
        }
        break;
    case Op::Php: push8(p_); break;
    case Op::Plp:
        p_ = pull8();
        if (emulation_) {
            p_ |= static_cast<std::uint8_t>(kIndex8 | kMemory8);
        }
        if (x8()) {
            x_ &= 0x00FF;
            y_ &= 0x00FF;
        }
        break;
    case Op::Phb: push8(dbr_); break;
    case Op::Plb:
        dbr_ = pull8();
        if (emulation_) {
            dbr_ = 0;
        }
        break;
    case Op::Phd: push16(d_); break;
    case Op::Pld:
        d_ = pull16();
        setZeroNegative16(d_);
        if (emulation_) {
            d_ = 0;
        }
        break;
    case Op::Phk: push8(pbr_); break;
    case Op::Rep:
    case Op::Sep: {
        const std::uint8_t mask = fetchByte();
        if (entry.op == Op::Rep) {
            p_ &= static_cast<std::uint8_t>(~mask);
        } else {
            p_ |= mask;
        }
        if (emulation_) {
            p_ |= static_cast<std::uint8_t>(kIndex8 | kMemory8);
        }
        if (x8()) {
            x_ &= 0x00FF;
            y_ &= 0x00FF;
        }
        break;
    }
    case Op::Stp:
        stopped_ = true;
        break;
    case Op::Wai:
        waiting_ = true;
        break;
    case Op::Tax: storeX(static_cast<std::uint16_t>(c_ & accMask)); break;
    case Op::Tay: storeY(static_cast<std::uint16_t>(c_ & accMask)); break;
    case Op::Txa: result(x_); break;
    case Op::Tya: result(y_); break;
    case Op::Txy: storeY(x_); break;
    case Op::Tyx: storeX(y_); break;
    case Op::Tsx: storeX(s_); break;
    case Op::Txs:
        s_ = x8() ? static_cast<std::uint16_t>(0x0100 | (x_ & 0xFF)) : x_;
        break;
    case Op::Tcd:
        d_ = c_;
        setZeroNegative16(d_);
        if (emulation_) {
            d_ = 0;
        }
        break;
    case Op::Tdc: result(d_); break;
    case Op::Tcs:
        s_ = c_;
        if (emulation_) {
            s_ = static_cast<std::uint16_t>(0x0100 | (s_ & 0xFF));
        }
        break;
    case Op::Tsc: result(s_); break;
    case Op::Xba: {
        const std::uint16_t low = static_cast<std::uint16_t>(c_ & 0xFF);
        c_ = static_cast<std::uint16_t>((c_ >> 8) | (low << 8));
        setZeroNegative8(static_cast<std::uint8_t>(c_ & 0xFF));
        break;
    }
    case Op::Xce:
        exchangeEmulation();
        break;
    }

    switch (entry.op) {
    case Op::Adc:
    case Op::And:
    case Op::Bit:
    case Op::Cmp:
    case Op::Eor:
    case Op::Lda:
    case Op::Ldx:
    case Op::Ldy:
    case Op::Ora:
    case Op::Sbc:
        // An indexed read with a sixteen-bit index always takes the extra
        // cycle; with an eight-bit index it takes one only across a page.
        switch (entry.mode) {
        case Mode::AbsX:
        case Mode::AbsY:
        case Mode::DpX:
        case Mode::DpY:
        case Mode::DpIndY:
        case Mode::SrSY:
            if (!x8() || crossedPage) {
                ++cycles;
            }
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    cyclesTotal_ += cycles;
    return cycles;
}

}  // namespace timewarp::hb2
