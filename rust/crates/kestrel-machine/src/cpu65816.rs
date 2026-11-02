// SPDX-License-Identifier: MIT
//
// The Kestrel 16's processor: a 65816.
//
// `TABLE` is the processor's own map, one row per opcode, and it was generated
// from the C++ core's `kOps` so the two trees cannot disagree about what an
// opcode does or what it costs. The mode is part of the row because on this
// processor the same opcode is a different width depending on a flag: `LDA
// #$1234` is three bytes in sixteen-bit mode and two in eight-bit mode, which is
// the single fact that most often makes a hand-written ROM dead.
//
// Three things are worth knowing before reading further:
//
//   * width. `m8()` is the accumulator's width and `x8()` the index registers',
//     and both are forced on in emulation mode. Everywhere a value is read,
//     written, pushed or compared, the width is decided by one of those two;
//   * the direct page. A direct page whose low byte is not zero straddles a page
//     boundary, and every access through it costs a cycle;
//   * `JMP` stays in the program bank and only `JML` sets one. A core that took
//     the bank from the operand made every jump from a bank-1 ROM land in bank
//     zero, which is a machine that does not run.

/// The 24-bit address space as the processor sees it.
pub trait Bus {
    fn read(&mut self, address: u32) -> u8;
    fn write(&mut self, address: u32, value: u8);
}

pub const CARRY: u8 = 0x01;
pub const ZERO: u8 = 0x02;
pub const IRQ_DISABLE: u8 = 0x04;
pub const DECIMAL: u8 = 0x08;
pub const INDEX_8: u8 = 0x10;
pub const MEMORY_8: u8 = 0x20;
pub const OVERFLOW: u8 = 0x40;
pub const NEGATIVE: u8 = 0x80;

/// What the processor is holding. A snapshot, for the monitor and tests.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Registers {
    pub c: u16,
    pub x: u16,
    pub y: u16,
    pub s: u16,
    pub d: u16,
    pub pc: u16,
    pub dbr: u8,
    pub pbr: u8,
    pub p: u8,
    pub emulation: bool,
}

impl Default for Registers {
    fn default() -> Self {
        Self {
            c: 0,
            x: 0,
            y: 0,
            s: 0x01FF,
            d: 0,
            pc: 0,
            dbr: 0,
            pbr: 0,
            p: 0x34,
            emulation: true,
        }
    }
}

// Vectors. The native ones are four bytes wide and sit below the emulation
// ones, which are the 6502's own and are all the emulation mode can reach.
const NATIVE_COP: u16 = 0xFFE4;
const NATIVE_BRK: u16 = 0xFFE6;
const NATIVE_IRQ: u16 = 0xFFEE;
const NATIVE_NMI: u16 = 0xFFEA;
const EMULATION_IRQ_BRK: u16 = 0xFFFE;
const EMULATION_NMI: u16 = 0xFFFA;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Mode {
    Imp, Acc, ImmM, ImmX, Imm8, Imm16, Sig, Dp, DpX, DpY, DpInd, DpIndL,
    DpIndX, DpIndY, DpIndLY, SrS, SrSY, Abs, AbsX, AbsY, AbsInd, AbsIndL,
    AbsIndX, Long, LongX, Rel, Rel16, Blk,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Op {
    Adc, And, Asl, Bcc, Bcs, Beq, Bit, Bmi, Bne, Bpl, Bra, Brk, Brl, Bvc,
    Bvs, Clc, Cld, Cli, Clv, Cmp, Cop, Cpx, Cpy, Dec, Dex, Dey, Eor, Inc,
    Inx, Iny, Jml, Jmp, Jsl, Jsr, Lda, Ldx, Ldy, Lsr, Mvn, Mvp, Nop, Ora,
    Pea, Pei, Per, Pha, Phb, Phd, Phk, Php, Phx, Phy, Pla, Plb, Pld, Plp,
    Plx, Ply, Rep, Rol, Ror, Rti, Rtl, Rts, Sbc, Sec, Sed, Sei, Sep, Sta,
    Stp, Stx, Sty, Stz, Tax, Tay, Tcd, Tcs, Tdc, Trb, Tsb, Tsc, Tsx, Txa,
    Txs, Txy, Tya, Tyx, Wai, Wdm, Xba, Xce,
}

struct Entry {
    op: Op,
    mode: Mode,
    cycles: u8,
}

#[rustfmt::skip]
static TABLE: [Entry; 256] = [
    Entry { op: Op::Brk, mode: Mode::Sig, cycles: 7 },
    Entry { op: Op::Ora, mode: Mode::DpIndX, cycles: 6 },
    Entry { op: Op::Cop, mode: Mode::Sig, cycles: 7 },
    Entry { op: Op::Ora, mode: Mode::SrS, cycles: 4 },
    Entry { op: Op::Tsb, mode: Mode::Dp, cycles: 5 },
    Entry { op: Op::Ora, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::Asl, mode: Mode::Dp, cycles: 5 },
    Entry { op: Op::Ora, mode: Mode::DpIndL, cycles: 6 },
    Entry { op: Op::Php, mode: Mode::Imp, cycles: 3 },
    Entry { op: Op::Ora, mode: Mode::ImmM, cycles: 2 },
    Entry { op: Op::Asl, mode: Mode::Acc, cycles: 2 },
    Entry { op: Op::Phd, mode: Mode::Imp, cycles: 4 },
    Entry { op: Op::Tsb, mode: Mode::Abs, cycles: 6 },
    Entry { op: Op::Ora, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Asl, mode: Mode::Abs, cycles: 6 },
    Entry { op: Op::Ora, mode: Mode::Long, cycles: 5 },
    Entry { op: Op::Bpl, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::Ora, mode: Mode::DpIndY, cycles: 5 },
    Entry { op: Op::Ora, mode: Mode::DpInd, cycles: 5 },
    Entry { op: Op::Ora, mode: Mode::SrSY, cycles: 7 },
    Entry { op: Op::Trb, mode: Mode::Dp, cycles: 5 },
    Entry { op: Op::Ora, mode: Mode::DpX, cycles: 4 },
    Entry { op: Op::Asl, mode: Mode::DpX, cycles: 6 },
    Entry { op: Op::Ora, mode: Mode::DpIndLY, cycles: 6 },
    Entry { op: Op::Clc, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Ora, mode: Mode::AbsY, cycles: 4 },
    Entry { op: Op::Inc, mode: Mode::Acc, cycles: 2 },
    Entry { op: Op::Tcs, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Trb, mode: Mode::Abs, cycles: 6 },
    Entry { op: Op::Ora, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Asl, mode: Mode::AbsX, cycles: 7 },
    Entry { op: Op::Ora, mode: Mode::LongX, cycles: 6 },
    Entry { op: Op::Jsr, mode: Mode::Abs, cycles: 6 },
    Entry { op: Op::And, mode: Mode::DpIndX, cycles: 6 },
    Entry { op: Op::Jsl, mode: Mode::Long, cycles: 8 },
    Entry { op: Op::And, mode: Mode::SrS, cycles: 4 },
    Entry { op: Op::Bit, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::And, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::Rol, mode: Mode::Dp, cycles: 5 },
    Entry { op: Op::And, mode: Mode::DpIndL, cycles: 6 },
    Entry { op: Op::Plp, mode: Mode::Imp, cycles: 4 },
    Entry { op: Op::And, mode: Mode::ImmM, cycles: 2 },
    Entry { op: Op::Rol, mode: Mode::Acc, cycles: 2 },
    Entry { op: Op::Pld, mode: Mode::Imp, cycles: 5 },
    Entry { op: Op::Bit, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::And, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Rol, mode: Mode::Abs, cycles: 6 },
    Entry { op: Op::And, mode: Mode::Long, cycles: 5 },
    Entry { op: Op::Bmi, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::And, mode: Mode::DpIndY, cycles: 5 },
    Entry { op: Op::And, mode: Mode::DpInd, cycles: 5 },
    Entry { op: Op::And, mode: Mode::SrSY, cycles: 7 },
    Entry { op: Op::Bit, mode: Mode::DpX, cycles: 4 },
    Entry { op: Op::And, mode: Mode::DpX, cycles: 4 },
    Entry { op: Op::Rol, mode: Mode::DpX, cycles: 6 },
    Entry { op: Op::And, mode: Mode::DpIndLY, cycles: 6 },
    Entry { op: Op::Sec, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::And, mode: Mode::AbsY, cycles: 4 },
    Entry { op: Op::Dec, mode: Mode::Acc, cycles: 2 },
    Entry { op: Op::Tsc, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Bit, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::And, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Rol, mode: Mode::AbsX, cycles: 7 },
    Entry { op: Op::And, mode: Mode::LongX, cycles: 6 },
    Entry { op: Op::Rti, mode: Mode::Imp, cycles: 6 },
    Entry { op: Op::Eor, mode: Mode::DpIndX, cycles: 6 },
    Entry { op: Op::Wdm, mode: Mode::Sig, cycles: 2 },
    Entry { op: Op::Eor, mode: Mode::SrS, cycles: 4 },
    Entry { op: Op::Mvp, mode: Mode::Blk, cycles: 7 },
    Entry { op: Op::Eor, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::Lsr, mode: Mode::Dp, cycles: 5 },
    Entry { op: Op::Eor, mode: Mode::DpIndL, cycles: 6 },
    Entry { op: Op::Pha, mode: Mode::Imp, cycles: 3 },
    Entry { op: Op::Eor, mode: Mode::ImmM, cycles: 2 },
    Entry { op: Op::Lsr, mode: Mode::Acc, cycles: 2 },
    Entry { op: Op::Phk, mode: Mode::Imp, cycles: 3 },
    Entry { op: Op::Jmp, mode: Mode::Abs, cycles: 3 },
    Entry { op: Op::Eor, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Lsr, mode: Mode::Abs, cycles: 6 },
    Entry { op: Op::Eor, mode: Mode::Long, cycles: 5 },
    Entry { op: Op::Bvc, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::Eor, mode: Mode::DpIndY, cycles: 5 },
    Entry { op: Op::Eor, mode: Mode::DpInd, cycles: 5 },
    Entry { op: Op::Eor, mode: Mode::SrSY, cycles: 7 },
    Entry { op: Op::Mvn, mode: Mode::Blk, cycles: 7 },
    Entry { op: Op::Eor, mode: Mode::DpX, cycles: 4 },
    Entry { op: Op::Lsr, mode: Mode::DpX, cycles: 6 },
    Entry { op: Op::Eor, mode: Mode::DpIndLY, cycles: 6 },
    Entry { op: Op::Cli, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Eor, mode: Mode::AbsY, cycles: 4 },
    Entry { op: Op::Phy, mode: Mode::Imp, cycles: 3 },
    Entry { op: Op::Tcd, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Jml, mode: Mode::Long, cycles: 5 },
    Entry { op: Op::Eor, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Lsr, mode: Mode::AbsX, cycles: 7 },
    Entry { op: Op::Eor, mode: Mode::LongX, cycles: 6 },
    Entry { op: Op::Rts, mode: Mode::Imp, cycles: 6 },
    Entry { op: Op::Adc, mode: Mode::DpIndX, cycles: 6 },
    Entry { op: Op::Per, mode: Mode::Rel16, cycles: 6 },
    Entry { op: Op::Adc, mode: Mode::SrS, cycles: 4 },
    Entry { op: Op::Stz, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::Adc, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::Ror, mode: Mode::Dp, cycles: 5 },
    Entry { op: Op::Adc, mode: Mode::DpIndL, cycles: 6 },
    Entry { op: Op::Pla, mode: Mode::Imp, cycles: 4 },
    Entry { op: Op::Adc, mode: Mode::ImmM, cycles: 2 },
    Entry { op: Op::Ror, mode: Mode::Acc, cycles: 2 },
    Entry { op: Op::Rtl, mode: Mode::Imp, cycles: 6 },
    Entry { op: Op::Jmp, mode: Mode::AbsInd, cycles: 5 },
    Entry { op: Op::Adc, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Ror, mode: Mode::Abs, cycles: 6 },
    Entry { op: Op::Adc, mode: Mode::Long, cycles: 5 },
    Entry { op: Op::Bvs, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::Adc, mode: Mode::DpIndY, cycles: 5 },
    Entry { op: Op::Adc, mode: Mode::DpInd, cycles: 5 },
    Entry { op: Op::Adc, mode: Mode::SrSY, cycles: 7 },
    Entry { op: Op::Stz, mode: Mode::DpX, cycles: 4 },
    Entry { op: Op::Adc, mode: Mode::DpX, cycles: 4 },
    Entry { op: Op::Ror, mode: Mode::DpX, cycles: 6 },
    Entry { op: Op::Adc, mode: Mode::DpIndLY, cycles: 6 },
    Entry { op: Op::Sei, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Adc, mode: Mode::AbsY, cycles: 4 },
    Entry { op: Op::Ply, mode: Mode::Imp, cycles: 4 },
    Entry { op: Op::Tdc, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Jmp, mode: Mode::AbsIndX, cycles: 6 },
    Entry { op: Op::Adc, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Ror, mode: Mode::AbsX, cycles: 7 },
    Entry { op: Op::Adc, mode: Mode::LongX, cycles: 6 },
    Entry { op: Op::Bra, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::Sta, mode: Mode::DpIndX, cycles: 6 },
    Entry { op: Op::Brl, mode: Mode::Rel16, cycles: 4 },
    Entry { op: Op::Sta, mode: Mode::SrS, cycles: 4 },
    Entry { op: Op::Sty, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::Sta, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::Stx, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::Sta, mode: Mode::DpIndL, cycles: 6 },
    Entry { op: Op::Dey, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Bit, mode: Mode::ImmM, cycles: 2 },
    Entry { op: Op::Txa, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Phb, mode: Mode::Imp, cycles: 3 },
    Entry { op: Op::Sty, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Sta, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Stx, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Sta, mode: Mode::Long, cycles: 5 },
    Entry { op: Op::Bcc, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::Sta, mode: Mode::DpIndY, cycles: 6 },
    Entry { op: Op::Sta, mode: Mode::DpInd, cycles: 5 },
    Entry { op: Op::Sta, mode: Mode::SrSY, cycles: 7 },
    Entry { op: Op::Sty, mode: Mode::DpX, cycles: 4 },
    Entry { op: Op::Sta, mode: Mode::DpX, cycles: 4 },
    Entry { op: Op::Stx, mode: Mode::DpY, cycles: 4 },
    Entry { op: Op::Sta, mode: Mode::DpIndLY, cycles: 6 },
    Entry { op: Op::Tya, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Sta, mode: Mode::AbsY, cycles: 5 },
    Entry { op: Op::Txs, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Txy, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Stz, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Sta, mode: Mode::AbsX, cycles: 5 },
    Entry { op: Op::Stz, mode: Mode::AbsX, cycles: 5 },
    Entry { op: Op::Sta, mode: Mode::LongX, cycles: 5 },
    Entry { op: Op::Ldy, mode: Mode::ImmX, cycles: 2 },
    Entry { op: Op::Lda, mode: Mode::DpIndX, cycles: 6 },
    Entry { op: Op::Ldx, mode: Mode::ImmX, cycles: 2 },
    Entry { op: Op::Lda, mode: Mode::SrS, cycles: 4 },
    Entry { op: Op::Ldy, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::Lda, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::Ldx, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::Lda, mode: Mode::DpIndL, cycles: 6 },
    Entry { op: Op::Tay, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Lda, mode: Mode::ImmM, cycles: 2 },
    Entry { op: Op::Tax, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Plb, mode: Mode::Imp, cycles: 4 },
    Entry { op: Op::Ldy, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Lda, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Ldx, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Lda, mode: Mode::Long, cycles: 5 },
    Entry { op: Op::Bcs, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::Lda, mode: Mode::DpIndY, cycles: 5 },
    Entry { op: Op::Lda, mode: Mode::DpInd, cycles: 5 },
    Entry { op: Op::Lda, mode: Mode::SrSY, cycles: 7 },
    Entry { op: Op::Ldy, mode: Mode::DpX, cycles: 4 },
    Entry { op: Op::Lda, mode: Mode::DpX, cycles: 4 },
    Entry { op: Op::Ldx, mode: Mode::DpY, cycles: 4 },
    Entry { op: Op::Lda, mode: Mode::DpIndLY, cycles: 6 },
    Entry { op: Op::Clv, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Lda, mode: Mode::AbsY, cycles: 4 },
    Entry { op: Op::Tsx, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Tyx, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Ldy, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Lda, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Ldx, mode: Mode::AbsY, cycles: 4 },
    Entry { op: Op::Lda, mode: Mode::LongX, cycles: 6 },
    Entry { op: Op::Cpy, mode: Mode::ImmX, cycles: 2 },
    Entry { op: Op::Cmp, mode: Mode::DpIndX, cycles: 6 },
    Entry { op: Op::Rep, mode: Mode::Imm8, cycles: 3 },
    Entry { op: Op::Cmp, mode: Mode::SrS, cycles: 4 },
    Entry { op: Op::Cpy, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::Cmp, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::Dec, mode: Mode::Dp, cycles: 5 },
    Entry { op: Op::Cmp, mode: Mode::DpIndL, cycles: 6 },
    Entry { op: Op::Iny, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Cmp, mode: Mode::ImmM, cycles: 2 },
    Entry { op: Op::Dex, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Wai, mode: Mode::Imp, cycles: 3 },
    Entry { op: Op::Cpy, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Cmp, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Dec, mode: Mode::Abs, cycles: 6 },
    Entry { op: Op::Cmp, mode: Mode::Long, cycles: 5 },
    Entry { op: Op::Bne, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::Cmp, mode: Mode::DpIndY, cycles: 5 },
    Entry { op: Op::Cmp, mode: Mode::DpInd, cycles: 5 },
    Entry { op: Op::Cmp, mode: Mode::SrSY, cycles: 7 },
    Entry { op: Op::Pei, mode: Mode::DpInd, cycles: 6 },
    Entry { op: Op::Cmp, mode: Mode::DpX, cycles: 4 },
    Entry { op: Op::Dec, mode: Mode::DpX, cycles: 6 },
    Entry { op: Op::Cmp, mode: Mode::DpIndLY, cycles: 6 },
    Entry { op: Op::Cld, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Cmp, mode: Mode::AbsY, cycles: 4 },
    Entry { op: Op::Phx, mode: Mode::Imp, cycles: 3 },
    Entry { op: Op::Stp, mode: Mode::Imp, cycles: 3 },
    Entry { op: Op::Jml, mode: Mode::AbsIndL, cycles: 6 },
    Entry { op: Op::Cmp, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Dec, mode: Mode::AbsX, cycles: 7 },
    Entry { op: Op::Cmp, mode: Mode::LongX, cycles: 6 },
    Entry { op: Op::Cpx, mode: Mode::ImmX, cycles: 2 },
    Entry { op: Op::Sbc, mode: Mode::DpIndX, cycles: 6 },
    Entry { op: Op::Sep, mode: Mode::Imm8, cycles: 3 },
    Entry { op: Op::Sbc, mode: Mode::SrS, cycles: 4 },
    Entry { op: Op::Cpx, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::Sbc, mode: Mode::Dp, cycles: 3 },
    Entry { op: Op::Inc, mode: Mode::Dp, cycles: 5 },
    Entry { op: Op::Sbc, mode: Mode::DpIndL, cycles: 6 },
    Entry { op: Op::Inx, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Sbc, mode: Mode::ImmM, cycles: 2 },
    Entry { op: Op::Nop, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Xba, mode: Mode::Imp, cycles: 3 },
    Entry { op: Op::Cpx, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Sbc, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Inc, mode: Mode::Abs, cycles: 6 },
    Entry { op: Op::Sbc, mode: Mode::Long, cycles: 5 },
    Entry { op: Op::Beq, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::Sbc, mode: Mode::DpIndY, cycles: 5 },
    Entry { op: Op::Sbc, mode: Mode::DpInd, cycles: 5 },
    Entry { op: Op::Sbc, mode: Mode::SrSY, cycles: 7 },
    Entry { op: Op::Pea, mode: Mode::Imm16, cycles: 5 },
    Entry { op: Op::Sbc, mode: Mode::DpX, cycles: 4 },
    Entry { op: Op::Inc, mode: Mode::DpX, cycles: 6 },
    Entry { op: Op::Sbc, mode: Mode::DpIndLY, cycles: 6 },
    Entry { op: Op::Sed, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Sbc, mode: Mode::AbsY, cycles: 4 },
    Entry { op: Op::Plx, mode: Mode::Imp, cycles: 4 },
    Entry { op: Op::Xce, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Jsr, mode: Mode::AbsIndX, cycles: 8 },
    Entry { op: Op::Sbc, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Inc, mode: Mode::AbsX, cycles: 7 },
    Entry { op: Op::Sbc, mode: Mode::LongX, cycles: 6 },
];

/// Where an operand lives, and whether finding it cost a cycle.
struct Effective {
    ea: u32,
    pointer_slot: u16,
    crossed_page: bool,
}

pub struct Cpu65816 {
    c: u16,
    x: u16,
    y: u16,
    s: u16,
    d: u16,
    pc: u16,
    dbr: u8,
    pbr: u8,
    p: u8,
    emulation: bool,
    stopped: bool,
    waiting: bool,
    irq_line: bool,
    nmi_pending: bool,
    cycles_total: u64,
    instructions: u64,
    illegal_opcodes: u64,
    last_illegal_opcode: u8,
}

impl Default for Cpu65816 {
    fn default() -> Self {
        Self::new()
    }
}

impl Cpu65816 {
    /// The power-on state: emulation mode, as the chip leaves reset, with PC
    /// from the reset vector in bank zero.
    pub fn new() -> Self {
        Self {
            c: 0,
            x: 0,
            y: 0,
            s: 0x01FF,
            d: 0,
            pc: 0,
            dbr: 0,
            pbr: 0,
            p: IRQ_DISABLE | INDEX_8 | MEMORY_8,
            emulation: true,
            stopped: false,
            waiting: false,
            irq_line: false,
            nmi_pending: false,
            cycles_total: 0,
            instructions: 0,
            illegal_opcodes: 0,
            last_illegal_opcode: 0,
        }
    }

    pub fn reset(&mut self, bus: &mut dyn Bus) {
        self.c = 0;
        self.x = 0;
        self.y = 0;
        self.s = 0x01FF;
        self.d = 0;
        self.dbr = 0;
        self.pbr = 0;
        self.emulation = true;
        self.p = IRQ_DISABLE | INDEX_8 | MEMORY_8;
        self.pc = self.read_pointer(bus, 0xFFFC);
        self.stopped = false;
        self.waiting = false;
        self.irq_line = false;
        self.nmi_pending = false;
    }

    pub fn registers(&self) -> Registers {
        Registers {
            c: self.c,
            x: self.x,
            y: self.y,
            s: self.s,
            d: self.d,
            pc: self.pc,
            dbr: self.dbr,
            pbr: self.pbr,
            p: self.p,
            emulation: self.emulation,
        }
    }

    pub fn set_registers(&mut self, registers: &Registers) {
        self.c = registers.c;
        self.x = registers.x;
        self.y = registers.y;
        self.s = registers.s;
        self.d = registers.d;
        self.pc = registers.pc;
        self.dbr = registers.dbr;
        self.pbr = registers.pbr;
        self.p = registers.p;
        self.emulation = registers.emulation;
        if self.emulation {
            self.p |= INDEX_8 | MEMORY_8;
            self.s = 0x0100 | (self.s & 0xFF);
            self.x &= 0x00FF;
            self.y &= 0x00FF;
            self.d = 0;
        }
    }

    /// The accumulator as the program sees it: eight bits or sixteen.
    pub fn c_low(&self) -> u8 {
        self.c as u8
    }

    pub fn c(&self) -> u16 {
        self.c
    }

    pub fn emulation(&self) -> bool {
        self.emulation
    }

    pub fn status(&self) -> u8 {
        self.p
    }

    pub fn set_irq_line(&mut self, level: bool) {
        self.irq_line = level;
    }

    pub fn request_nmi(&mut self) {
        self.nmi_pending = true;
    }

    pub fn cycle_count(&self) -> u64 {
        self.cycles_total
    }

    pub fn instruction_count(&self) -> u64 {
        self.instructions
    }

    pub fn illegal_opcodes(&self) -> u64 {
        self.illegal_opcodes
    }

    pub fn last_illegal_opcode(&self) -> u8 {
        self.last_illegal_opcode
    }

    fn m8(&self) -> bool {
        self.emulation || self.p & MEMORY_8 != 0
    }

    fn x8(&self) -> bool {
        self.emulation || self.p & INDEX_8 != 0
    }

    fn flag(&self, bit: u8) -> bool {
        self.p & bit != 0
    }

    fn set_flag(&mut self, bit: u8, on: bool) {
        if on {
            self.p |= bit;
        } else {
            self.p &= !bit;
        }
    }

    fn set_zn8(&mut self, value: u8) {
        self.set_flag(ZERO, value == 0);
        self.set_flag(NEGATIVE, value & 0x80 != 0);
    }

    fn set_zn16(&mut self, value: u16) {
        self.set_flag(ZERO, value == 0);
        self.set_flag(NEGATIVE, value & 0x8000 != 0);
    }

    fn read8(&self, bus: &mut dyn Bus, address: u32) -> u8 {
        bus.read(address & 0xFF_FFFF)
    }

    fn write8(&self, bus: &mut dyn Bus, address: u32, value: u8) {
        bus.write(address & 0xFF_FFFF, value);
    }

    /// A pointer never leaves bank zero, and its high byte wraps inside the
    /// bank: `$FFFF` reads `$FFFF` and `$0000`.
    fn read_pointer(&self, bus: &mut dyn Bus, offset: u16) -> u16 {
        let low = u16::from(self.read8(bus, u32::from(offset)));
        let high = u16::from(self.read8(bus, u32::from(offset.wrapping_add(1))));
        low | (high << 8)
    }

    /// A pointer read from the program bank, which is where `JMP (abs,X)` and
    /// `JSR (abs,X)` keep theirs. The plain sixteen-bit pointer read stays in
    /// bank zero, which is why the two are separate functions.
    fn read_pointer_in_code_bank(&self, bus: &mut dyn Bus, slot: u16) -> u32 {
        let bank = u32::from(self.pbr) << 16;
        let low = u32::from(self.read8(bus, bank | u32::from(slot)));
        let high = u32::from(self.read8(bus, bank | u32::from(slot.wrapping_add(1))));
        low | (high << 8)
    }

    fn fetch_byte(&mut self, bus: &mut dyn Bus) -> u8 {
        let address = self.code_address(self.pc);
        let byte = self.read8(bus, address);
        self.pc = self.pc.wrapping_add(1);
        byte
    }

    fn fetch_word(&mut self, bus: &mut dyn Bus) -> u16 {
        let low = u16::from(self.fetch_byte(bus));
        let high = u16::from(self.fetch_byte(bus));
        low | (high << 8)
    }

    fn fetch_operand_data(&mut self, bus: &mut dyn Bus) -> u16 {
        if self.m8() {
            u16::from(self.fetch_byte(bus))
        } else {
            self.fetch_word(bus)
        }
    }

    fn fetch_operand_index(&mut self, bus: &mut dyn Bus) -> u16 {
        if self.x8() {
            u16::from(self.fetch_byte(bus))
        } else {
            self.fetch_word(bus)
        }
    }

    fn push8(&mut self, bus: &mut dyn Bus, value: u8) {
        let address = 0x0100 | u32::from(self.s);
        self.write8(bus, address, value);
        self.s = self.s.wrapping_sub(1);
    }

    fn pull8(&mut self, bus: &mut dyn Bus) -> u8 {
        self.s = self.s.wrapping_add(1);
        let address = 0x0100 | u32::from(self.s);
        self.read8(bus, address)
    }

    fn push16(&mut self, bus: &mut dyn Bus, value: u16) {
        self.push8(bus, (value >> 8) as u8);
        self.push8(bus, value as u8);
    }

    fn pull16(&mut self, bus: &mut dyn Bus) -> u16 {
        let low = u16::from(self.pull8(bus));
        let high = u16::from(self.pull8(bus));
        low | (high << 8)
    }

    fn data_address(&self, offset: u16) -> u32 {
        (u32::from(self.dbr) << 16) | u32::from(offset)
    }

    fn code_address(&self, offset: u16) -> u32 {
        (u32::from(self.pbr) << 16) | u32::from(offset)
    }

    fn read_data(&self, bus: &mut dyn Bus, address: u32) -> u16 {
        if self.m8() {
            return u16::from(self.read8(bus, address));
        }
        let low = u16::from(self.read8(bus, address));
        let high = u16::from(self.read8(bus, (address & 0xFF_0000) | ((address + 1) & 0xFFFF)));
        low | (high << 8)
    }

    /// An operand of the index file: eight or sixteen bits, decided by X and
    /// never by M. `LDX`, `LDY`, `STX`, `STY` and the memory forms of `CPX`/`CPY`
    /// are the instructions that use it, and getting it wrong loses the high byte
    /// of every address above `$FF` they touch.
    fn read_index(&self, bus: &mut dyn Bus, address: u32) -> u16 {
        if self.x8() {
            return u16::from(self.read8(bus, address));
        }
        let low = u16::from(self.read8(bus, address));
        let high = u16::from(self.read8(bus, (address & 0xFF_0000) | ((address + 1) & 0xFFFF)));
        low | (high << 8)
    }

    fn write_index(&self, bus: &mut dyn Bus, address: u32, value: u16) {
        self.write8(bus, address, value as u8);
        if !self.x8() {
            let high = (address & 0xFF_0000) | ((address + 1) & 0xFFFF);
            self.write8(bus, high, (value >> 8) as u8);
        }
    }

    fn write_data(&self, bus: &mut dyn Bus, address: u32, value: u16) {
        self.write8(bus, address, value as u8);
        if !self.m8() {
            let high = (address & 0xFF_0000) | ((address + 1) & 0xFFFF);
            self.write8(bus, high, (value >> 8) as u8);
        }
    }

    fn index_value(&self, register: u16) -> u16 {
        if self.x8() {
            register & 0xFF
        } else {
            register
        }
    }

    fn exchange_emulation(&mut self) {
        let carry = self.flag(CARRY);
        self.set_flag(CARRY, self.emulation);
        self.emulation = carry;
        if !self.emulation {
            return;
        }
        // Emulation mode is the 6502's: eight-bit registers, a stack page fixed
        // at $0100, a direct page fixed at zero, and banks zero.
        self.p |= INDEX_8 | MEMORY_8;
        self.s = 0x0100 | (self.s & 0xFF);
        self.x &= 0x00FF;
        self.y &= 0x00FF;
        self.d = 0;
        self.dbr = 0;
        self.pbr = 0;
    }

    fn interrupt(&mut self, bus: &mut dyn Bus, vector_offset: u16, software: bool) {
        self.waiting = false;
        if self.emulation {
            self.push8(bus, (self.pc >> 8) as u8);
            self.push8(bus, self.pc as u8);
            let pushed = if software { self.p | 0x10 } else { self.p & !0x10 };
            self.push8(bus, pushed);
            self.set_flag(IRQ_DISABLE, true);
            self.set_flag(DECIMAL, false);
            self.pbr = 0;
            self.pc = self.read_pointer(bus, vector_offset);
            return;
        }
        self.push8(bus, self.pbr);
        self.push8(bus, (self.pc >> 8) as u8);
        self.push8(bus, self.pc as u8);
        let pushed = if software { self.p } else { self.p | 0x20 };
        self.push8(bus, pushed);
        self.set_flag(IRQ_DISABLE, true);
        self.set_flag(DECIMAL, false);
        self.pbr = 0;
        self.dbr = 0;
        self.pc = self.read_pointer(bus, vector_offset);
    }

    fn adc(&mut self, value: u16) {
        let mask: u16 = if self.m8() { 0x00FF } else { 0xFFFF };
        let a = self.c & mask;
        let v = value & mask;
        let carry = i32::from(self.flag(CARRY));
        let binary = i32::from(a) + i32::from(v) + carry;
        let result = (binary as u16) & mask;
        let sign: u16 = if self.m8() { 0x80 } else { 0x8000 };

        self.set_flag(CARRY, i64::from(binary) > i64::from(mask));
        self.set_flag(OVERFLOW, (!(a ^ v) & (a ^ (binary as u16)) & sign) != 0);
        if self.m8() {
            self.set_zn8(result as u8);
        } else {
            self.set_zn16(result);
        }

        let mut stored = result;
        if self.flag(DECIMAL) {
            // One nibble at a time, which is what the decimal adjust does: add
            // the six whenever a digit passes nine, and carry into the next.
            let mut adjusted: u32 = 0;
            let mut carry_in = carry as u32;
            let mut shift = 0;
            let width = if self.m8() { 8 } else { 16 };
            while shift < width {
                let mut digit = ((a >> shift) & 0xF) as u32 + ((v >> shift) & 0xF) as u32 + carry_in;
                if digit > 9 {
                    digit += 6;
                }
                carry_in = u32::from(digit > 0xF);
                adjusted |= (digit & 0xF) << shift;
                shift += 4;
            }
            self.set_flag(CARRY, carry_in != 0);
            stored = adjusted as u16 & mask;
        }
        if self.m8() {
            self.c = (self.c & 0xFF00) | stored;
        } else {
            self.c = stored;
        }
    }

    fn sbc(&mut self, value: u16) {
        let mask: u16 = if self.m8() { 0x00FF } else { 0xFFFF };
        let a = self.c & mask;
        let v = value & mask;
        let borrow = i32::from(!self.flag(CARRY));
        let binary = i32::from(a) - i32::from(v) - borrow;
        let result = (binary as u16) & mask;
        let sign: u16 = if self.m8() { 0x80 } else { 0x8000 };

        self.set_flag(CARRY, binary >= 0);
        self.set_flag(OVERFLOW, ((a ^ v) & (a ^ (binary as u16)) & sign) != 0);
        if self.m8() {
            self.set_zn8(result as u8);
        } else {
            self.set_zn16(result);
        }

        let mut stored = result;
        if self.flag(DECIMAL) {
            let mut adjusted: u32 = 0;
            let mut borrow_in = borrow;
            let mut shift = 0;
            let width = if self.m8() { 8 } else { 16 };
            while shift < width {
                let mut digit = ((a >> shift) & 0xF) as i32 - ((v >> shift) & 0xF) as i32 - borrow_in;
                if digit < 0 {
                    digit -= 6;
                    borrow_in = 1;
                } else {
                    borrow_in = 0;
                }
                adjusted |= ((digit & 0xF) as u32) << shift;
                shift += 4;
            }
            self.set_flag(CARRY, borrow_in == 0);
            stored = adjusted as u16 & mask;
        }
        if self.m8() {
            self.c = (self.c & 0xFF00) | stored;
        } else {
            self.c = stored;
        }
    }

    fn compare(&mut self, register: u16, value: u16) {
        if self.m8() {
            let difference = i32::from(register & 0xFF) - i32::from(value & 0xFF);
            self.set_flag(CARRY, difference >= 0);
            self.set_zn8((difference & 0xFF) as u8);
            return;
        }
        let difference = i32::from(register) - i32::from(value);
        self.set_flag(CARRY, difference >= 0);
        self.set_zn16((difference & 0xFFFF) as u16);
    }

    fn asl_value(&mut self, value: u16) -> u16 {
        if self.m8() {
            self.set_flag(CARRY, value & 0x80 != 0);
            let result = (value << 1) as u8;
            self.set_zn8(result);
            return u16::from(result);
        }
        self.set_flag(CARRY, value & 0x8000 != 0);
        let result = value << 1;
        self.set_zn16(result);
        result
    }

    fn lsr_value(&mut self, value: u16) -> u16 {
        if self.m8() {
            self.set_flag(CARRY, value & 0x01 != 0);
            let result = (value as u8) >> 1;
            self.set_zn8(result);
            return u16::from(result);
        }
        self.set_flag(CARRY, value & 0x0001 != 0);
        let result = value >> 1;
        self.set_zn16(result);
        result
    }

    fn rol_value(&mut self, value: u16) -> u16 {
        let carry = self.flag(CARRY);
        if self.m8() {
            self.set_flag(CARRY, value & 0x80 != 0);
            let result = ((value << 1) | u16::from(carry)) as u8;
            self.set_zn8(result);
            return u16::from(result);
        }
        self.set_flag(CARRY, value & 0x8000 != 0);
        let result = (value << 1) | u16::from(carry);
        self.set_zn16(result);
        result
    }

    fn ror_value(&mut self, value: u16) -> u16 {
        let carry = self.flag(CARRY);
        if self.m8() {
            self.set_flag(CARRY, value & 0x01 != 0);
            let result = ((value as u8) >> 1) | if carry { 0x80 } else { 0 };
            self.set_zn8(result);
            return u16::from(result);
        }
        self.set_flag(CARRY, value & 0x0001 != 0);
        let result = (value >> 1) | if carry { 0x8000 } else { 0 };
        self.set_zn16(result);
        result
    }

    /// The accumulator, stored at the width the mode flag says.
    fn set_acc(&mut self, value: u16) {
        if self.m8() {
            self.c = (self.c & 0xFF00) | (value & 0xFF);
            self.set_zn8((value & 0xFF) as u8);
        } else {
            self.c = value;
            self.set_zn16(value);
        }
    }

    fn store_x(&mut self, value: u16) {
        if self.x8() {
            self.x = value & 0xFF;
            self.set_zn8((value & 0xFF) as u8);
        } else {
            self.x = value;
            self.set_zn16(value);
        }
    }

    fn store_y(&mut self, value: u16) {
        if self.x8() {
            self.y = value & 0xFF;
            self.set_zn8((value & 0xFF) as u8);
        } else {
            self.y = value;
            self.set_zn16(value);
        }
    }

    fn acc_mask(&self) -> u16 {
        if self.m8() {
            0x00FF
        } else {
            0xFFFF
        }
    }

    /// A direct page whose low byte is not zero straddles a page boundary, and
    /// every access through it costs a cycle.
    fn direct_page(&self, offset: u8, cycles: &mut i32) -> u16 {
        if self.d & 0xFF != 0 {
            *cycles += 1;
        }
        self.d.wrapping_add(u16::from(offset))
    }

    fn read_indirect_long(&self, bus: &mut dyn Bus, slot: u16) -> u32 {
        let low = u32::from(self.read_pointer(bus, slot));
        let high = u32::from(self.read8(bus, u32::from(slot.wrapping_add(2))));
        low | (high << 16)
    }

    fn address(&mut self, bus: &mut dyn Bus, mode: Mode, cycles: &mut i32) -> Effective {
        let mut effective = Effective {
            ea: 0,
            pointer_slot: 0,
            crossed_page: false,
        };
        match mode {
            Mode::Dp => {
                let offset = self.fetch_byte(bus);
                effective.ea = u32::from(self.direct_page(offset, cycles));
            }
            Mode::DpX | Mode::DpY => {
                let base = self.fetch_byte(bus);
                if self.d & 0xFF != 0 {
                    *cycles += 1;
                }
                let index = self.index_value(if mode == Mode::DpX { self.x } else { self.y });
                let offset = if self.d & 0xFF == 0 && self.x8() {
                    (self.d & 0xFF00) | u16::from(base.wrapping_add(index as u8))
                } else {
                    self.d.wrapping_add(u16::from(base)).wrapping_add(index)
                };
                effective.ea = u32::from(offset);
            }
            Mode::DpInd => {
                let offset = self.fetch_byte(bus);
                effective.pointer_slot = self.direct_page(offset, cycles);
                effective.ea = self.data_address(self.read_pointer(bus, effective.pointer_slot));
            }
            Mode::DpIndL => {
                let offset = self.fetch_byte(bus);
                effective.pointer_slot = self.direct_page(offset, cycles);
                effective.ea = self.read_indirect_long(bus, effective.pointer_slot);
            }
            Mode::DpIndX => {
                let offset = self.fetch_byte(bus).wrapping_add(self.x as u8);
                effective.pointer_slot = self.direct_page(offset, cycles);
                effective.ea = self.data_address(self.read_pointer(bus, effective.pointer_slot));
            }
            Mode::DpIndY | Mode::DpIndLY => {
                let offset = self.fetch_byte(bus);
                effective.pointer_slot = self.direct_page(offset, cycles);
                let pointer = if mode == Mode::DpIndY {
                    u32::from(self.read_pointer(bus, effective.pointer_slot))
                } else {
                    self.read_indirect_long(bus, effective.pointer_slot)
                };
                let offset = pointer.wrapping_add(u32::from(self.index_value(self.y)));
                if mode == Mode::DpIndY {
                    effective.crossed_page = (pointer & 0xFF00) != (offset & 0xFF00);
                    effective.ea = self.data_address(offset as u16);
                } else {
                    effective.ea = offset & 0xFF_FFFF;
                }
            }
            Mode::SrS => {
                let offset = self.fetch_byte(bus);
                effective.ea = u32::from(self.s.wrapping_add(u16::from(offset)));
            }
            Mode::SrSY => {
                let slot = self.s.wrapping_add(u16::from(self.fetch_byte(bus)));
                let base = self.read_pointer(bus, slot);
                let offset = base.wrapping_add(self.index_value(self.y));
                effective.crossed_page = (base & 0xFF00) != (offset & 0xFF00);
                effective.ea = self.data_address(offset);
            }
            Mode::Abs => {
                let offset = self.fetch_word(bus);
                effective.ea = self.data_address(offset);
            }
            Mode::AbsX | Mode::AbsY => {
                let base = self.fetch_word(bus);
                let index = self.index_value(if mode == Mode::AbsX { self.x } else { self.y });
                let offset = base.wrapping_add(index);
                effective.crossed_page = (base & 0xFF00) != (offset & 0xFF00);
                effective.ea = self.data_address(offset);
            }
            Mode::AbsInd => {
                let slot = self.fetch_word(bus);
                effective.ea = u32::from(self.read_pointer(bus, slot));
            }
            Mode::AbsIndX => {
                let slot = self.fetch_word(bus).wrapping_add(self.index_value(self.x));
                effective.ea = self.read_pointer_in_code_bank(bus, slot);
            }
            Mode::AbsIndL => {
                let slot = self.fetch_word(bus);
                effective.ea = self.read_indirect_long(bus, slot);
            }
            Mode::Long => {
                let low = u32::from(self.fetch_byte(bus));
                let middle = u32::from(self.fetch_byte(bus));
                let high = u32::from(self.fetch_byte(bus));
                effective.ea = low | (middle << 8) | (high << 16);
            }
            Mode::LongX => {
                let low = u32::from(self.fetch_byte(bus));
                let middle = u32::from(self.fetch_byte(bus));
                let high = u32::from(self.fetch_byte(bus));
                effective.ea =
                    (low | (middle << 8) | (high << 16)).wrapping_add(u32::from(self.index_value(self.x)))
                        & 0xFF_FFFF;
            }
            _ => {}
        }
        effective
    }

    /// Executes one instruction, or services a pending interrupt. Returns the
    /// cycles it took.
    pub fn step(&mut self, bus: &mut dyn Bus) -> u32 {
        if self.stopped {
            self.cycles_total += 1;
            return 1;
        }
        if self.nmi_pending {
            self.nmi_pending = false;
            let vector = if self.emulation { EMULATION_NMI } else { NATIVE_NMI };
            self.interrupt(bus, vector, false);
            self.instructions += 1;
            self.cycles_total += 8;
            return 8;
        }
        if self.irq_line && !self.flag(IRQ_DISABLE) {
            let vector = if self.emulation { EMULATION_IRQ_BRK } else { NATIVE_IRQ };
            self.interrupt(bus, vector, false);
            self.instructions += 1;
            self.cycles_total += 8;
            return 8;
        }
        if self.waiting {
            // `WAI` with no line to wake it: the processor idles a cycle at a
            // time.
            self.cycles_total += 1;
            return 1;
        }

        let opcode = self.fetch_byte(bus);
        let (op, mode) = {
            let entry = &TABLE[opcode as usize];
            (entry.op, entry.mode)
        };
        let mut cycles = i32::from(TABLE[opcode as usize].cycles);
        self.instructions += 1;

        let effective = self.address(bus, mode, &mut cycles);
        let ea = effective.ea;
        let acc_mask = self.acc_mask();

        match op {
            Op::Adc => {
                let value = self.operand(bus, mode, ea);
                self.adc(value);
            }
            Op::Sbc => {
                let value = self.operand(bus, mode, ea);
                self.sbc(value);
            }
            Op::And => {
                let value = self.operand(bus, mode, ea);
                self.set_acc((self.c & acc_mask) & value);
            }
            Op::Ora => {
                let value = self.operand(bus, mode, ea);
                self.set_acc((self.c & acc_mask) | value);
            }
            Op::Eor => {
                let value = self.operand(bus, mode, ea);
                self.set_acc((self.c & acc_mask) ^ value);
            }
            Op::Bit => {
                if mode == Mode::ImmM {
                    let value = self.fetch_operand_data(bus);
                    self.set_flag(ZERO, ((self.c & value) & acc_mask) == 0);
                } else {
                    let value = self.read_data(bus, ea);
                    self.set_flag(ZERO, ((self.c & value) & acc_mask) == 0);
                    if self.m8() {
                        self.set_flag(NEGATIVE, value & 0x80 != 0);
                        self.set_flag(OVERFLOW, value & 0x40 != 0);
                    } else {
                        self.set_flag(NEGATIVE, value & 0x8000 != 0);
                        self.set_flag(OVERFLOW, value & 0x4000 != 0);
                    }
                }
            }
            Op::Cmp => {
                let value = self.operand(bus, mode, ea);
                self.compare(self.c, value);
            }
            Op::Cpx | Op::Cpy => {
                let register = if op == Op::Cpx { self.x } else { self.y };
                let value = if mode == Mode::ImmX {
                    self.fetch_operand_index(bus)
                } else {
                    self.read_index(bus, ea)
                };
                let mask: u16 = if self.x8() { 0x00FF } else { 0xFFFF };
                let difference = i32::from(register & mask) - i32::from(value & mask);
                self.set_flag(CARRY, difference >= 0);
                if self.x8() {
                    self.set_zn8((difference & 0xFF) as u8);
                } else {
                    self.set_zn16((difference & 0xFFFF) as u16);
                }
            }
            Op::Asl | Op::Lsr | Op::Rol | Op::Ror => {
                let accumulator = mode == Mode::Acc;
                let value = if accumulator {
                    self.c & acc_mask
                } else {
                    self.read_data(bus, ea)
                };
                let shifted = match op {
                    Op::Asl => self.asl_value(value),
                    Op::Lsr => self.lsr_value(value),
                    Op::Rol => self.rol_value(value),
                    _ => self.ror_value(value),
                };
                if accumulator {
                    if self.m8() {
                        self.c = (self.c & 0xFF00) | shifted;
                    } else {
                        self.c = shifted;
                    }
                } else {
                    self.write_data(bus, ea, shifted);
                }
            }
            Op::Dec | Op::Inc => {
                let increment = op == Op::Inc;
                if mode == Mode::Acc {
                    let value = if increment {
                        (self.c & acc_mask).wrapping_add(1)
                    } else {
                        (self.c & acc_mask).wrapping_sub(1)
                    };
                    self.set_acc(value);
                } else {
                    // A memory increment is as wide as the accumulator, like
                    // everything else that reads and writes through M. This core
                    // used to force it to a byte, on the belief that the chip had
                    // no sixteen-bit INC; it has, and forcing the width set the
                    // flags from the wrong half of the word.
                    let old = self.read_data(bus, ea);
                    let value = if increment {
                        old.wrapping_add(1)
                    } else {
                        old.wrapping_sub(1)
                    };
                    self.write_data(bus, ea, value);
                    if self.m8() {
                        self.set_zn8(value as u8);
                    } else {
                        self.set_zn16(value);
                    }
                }
            }
            Op::Dex => {
                let value = self.x.wrapping_sub(1);
                self.store_x(value);
            }
            Op::Dey => {
                let value = self.y.wrapping_sub(1);
                self.store_y(value);
            }
            Op::Inx => {
                let value = self.x.wrapping_add(1);
                self.store_x(value);
            }
            Op::Iny => {
                let value = self.y.wrapping_add(1);
                self.store_y(value);
            }
            Op::Lda => {
                let value = self.operand(bus, mode, ea);
                self.set_acc(value);
            }
            Op::Ldx => {
                let value = if mode == Mode::ImmX {
                    self.fetch_operand_index(bus)
                } else {
                    self.read_index(bus, ea)
                };
                self.store_x(value);
            }
            Op::Ldy => {
                let value = if mode == Mode::ImmX {
                    self.fetch_operand_index(bus)
                } else {
                    self.read_index(bus, ea)
                };
                self.store_y(value);
            }
            Op::Sta => {
                let value = self.c & acc_mask;
                self.write_data(bus, ea, value);
            }
            Op::Stx => self.write_index(bus, ea, self.x),
            Op::Sty => self.write_index(bus, ea, self.y),
            Op::Stz => self.write_data(bus, ea, 0),
            Op::Tsb => {
                let value = self.read_data(bus, ea);
                self.set_flag(ZERO, ((value & self.c) & acc_mask) == 0);
                let stored = value | (self.c & acc_mask);
                self.write_data(bus, ea, stored);
            }
            Op::Trb => {
                let value = self.read_data(bus, ea);
                self.set_flag(ZERO, ((value & self.c) & acc_mask) == 0);
                let stored = value & !(self.c & acc_mask);
                self.write_data(bus, ea, stored);
            }
            Op::Bcc | Op::Bcs | Op::Beq | Op::Bmi | Op::Bne | Op::Bpl | Op::Bvc | Op::Bvs
            | Op::Bra => {
                let offset = self.fetch_byte(bus) as i8;
                let taken = match op {
                    Op::Bcc => !self.flag(CARRY),
                    Op::Bcs => self.flag(CARRY),
                    Op::Beq => self.flag(ZERO),
                    Op::Bmi => self.flag(NEGATIVE),
                    Op::Bne => !self.flag(ZERO),
                    Op::Bpl => !self.flag(NEGATIVE),
                    Op::Bvc => !self.flag(OVERFLOW),
                    Op::Bvs => self.flag(OVERFLOW),
                    _ => true,
                };
                if taken {
                    let target = self.pc.wrapping_add(offset as u16);
                    cycles += 1; // a taken branch is one cycle longer
                    if (target & 0xFF00) != (self.pc & 0xFF00) {
                        cycles += 1; // and one more when it leaves the page
                    }
                    self.pc = target;
                }
            }
            Op::Brl => {
                let offset = self.fetch_word(bus) as i16;
                self.pc = self.pc.wrapping_add(offset as u16);
            }
            Op::Brk => {
                let _signature = self.fetch_byte(bus);
                let vector = if self.emulation { EMULATION_IRQ_BRK } else { NATIVE_BRK };
                self.interrupt(bus, vector, true);
            }
            Op::Cop => {
                let _signature = self.fetch_byte(bus);
                let vector = if self.emulation { EMULATION_IRQ_BRK } else { NATIVE_COP };
                self.interrupt(bus, vector, true);
            }
            Op::Wdm => {
                let _signature = self.fetch_byte(bus);
            }
            Op::Clc => self.set_flag(CARRY, false),
            Op::Sec => self.set_flag(CARRY, true),
            Op::Cld => self.set_flag(DECIMAL, false),
            Op::Sed => self.set_flag(DECIMAL, true),
            Op::Cli => self.set_flag(IRQ_DISABLE, false),
            Op::Sei => self.set_flag(IRQ_DISABLE, true),
            Op::Clv => self.set_flag(OVERFLOW, false),
            Op::Jmp | Op::Jml => {
                // JMP stays in the program bank: the bank of the effective
                // address is DBR, which has nothing to do with where the jump
                // lands. Taking it from the address sent a jump in a bank-1 ROM
                // to bank zero.
                if op == Op::Jml {
                    self.pbr = (ea >> 16) as u8;
                }
                self.pc = ea as u16;
            }
            Op::Jsr | Op::Jsl => {
                if op == Op::Jsl {
                    let pbr = self.pbr;
                    self.push8(bus, pbr);
                }
                let return_to = self.pc.wrapping_sub(1);
                self.push16(bus, return_to);
                if op == Op::Jsl {
                    self.pbr = (ea >> 16) as u8;
                }
                self.pc = ea as u16;
            }
            Op::Rts => {
                let target = self.pull16(bus).wrapping_add(1);
                self.pc = target;
            }
            Op::Rtl => {
                let target = self.pull16(bus).wrapping_add(1);
                self.pc = target;
                self.pbr = self.pull8(bus);
            }
            Op::Rti => {
                self.p = self.pull8(bus);
                if self.emulation {
                    self.p |= INDEX_8 | MEMORY_8;
                }
                self.pc = self.pull16(bus);
                if self.emulation {
                    self.pbr = 0;
                } else {
                    self.pbr = self.pull8(bus);
                }
                if self.x8() {
                    self.x &= 0x00FF;
                    self.y &= 0x00FF;
                }
            }
            Op::Mvn | Op::Mvp => {
                let destination = self.fetch_byte(bus);
                let source = self.fetch_byte(bus);
                let step: i32 = if op == Op::Mvn { 1 } else { -1 };
                let count = u32::from(if self.x8() { self.c & 0xFF } else { self.c }) + 1;
                let mut from = (u32::from(source) << 16) | u32::from(self.x);
                let mut to = (u32::from(destination) << 16) | u32::from(self.y);
                for _ in 0..count {
                    let byte = self.read8(bus, from);
                    self.write8(bus, to, byte);
                    from = (from & 0xFF_0000) | u32::from((from as u16).wrapping_add(step as u16));
                    to = (to & 0xFF_0000) | u32::from((to as u16).wrapping_add(step as u16));
                }
                self.x = from as u16;
                self.y = to as u16;
                if self.x8() {
                    self.x &= 0x00FF;
                    self.y &= 0x00FF;
                }
                self.c = 0xFFFF; // the count runs out, which is what the chip leaves
                self.dbr = destination;
            }
            Op::Nop => {}
            Op::Pea => {
                let value = self.fetch_word(bus);
                self.push16(bus, value);
            }
            Op::Pei => {
                let value = self.read_pointer(bus, effective.pointer_slot);
                self.push16(bus, value);
            }
            Op::Per => {
                let offset = self.fetch_word(bus) as i16;
                let value = self.pc.wrapping_add(offset as u16);
                self.push16(bus, value);
            }
            Op::Pha => {
                if self.m8() {
                    let value = self.c as u8;
                    self.push8(bus, value);
                } else {
                    let value = self.c;
                    self.push16(bus, value);
                }
            }
            Op::Pla => {
                if self.m8() {
                    let value = self.pull8(bus);
                    self.c = (self.c & 0xFF00) | u16::from(value);
                    self.set_zn8(value);
                } else {
                    let value = self.pull16(bus);
                    self.c = value;
                    self.set_zn16(value);
                }
            }
            Op::Phx => {
                if self.x8() {
                    let value = self.x as u8;
                    self.push8(bus, value);
                } else {
                    let value = self.x;
                    self.push16(bus, value);
                }
            }
            Op::Plx => {
                if self.x8() {
                    let value = self.pull8(bus);
                    self.x = u16::from(value);
                    self.set_zn8(value);
                } else {
                    let value = self.pull16(bus);
                    self.x = value;
                    self.set_zn16(value);
                }
            }
            Op::Phy => {
                if self.x8() {
                    let value = self.y as u8;
                    self.push8(bus, value);
                } else {
                    let value = self.y;
                    self.push16(bus, value);
                }
            }
            Op::Ply => {
                if self.x8() {
                    let value = self.pull8(bus);
                    self.y = u16::from(value);
                    self.set_zn8(value);
                } else {
                    let value = self.pull16(bus);
                    self.y = value;
                    self.set_zn16(value);
                }
            }
            Op::Php => {
                let value = self.p;
                self.push8(bus, value);
            }
            Op::Plp => {
                self.p = self.pull8(bus);
                if self.emulation {
                    self.p |= INDEX_8 | MEMORY_8;
                }
                if self.x8() {
                    self.x &= 0x00FF;
                    self.y &= 0x00FF;
                }
            }
            Op::Phb => {
                let value = self.dbr;
                self.push8(bus, value);
            }
            Op::Plb => {
                self.dbr = self.pull8(bus);
                if self.emulation {
                    self.dbr = 0;
                }
            }
            Op::Phd => {
                let value = self.d;
                self.push16(bus, value);
            }
            Op::Pld => {
                self.d = self.pull16(bus);
                self.set_zn16(self.d);
                if self.emulation {
                    self.d = 0;
                }
            }
            Op::Phk => {
                let value = self.pbr;
                self.push8(bus, value);
            }
            Op::Rep | Op::Sep => {
                let mask = self.fetch_byte(bus);
                if op == Op::Rep {
                    self.p &= !mask;
                } else {
                    self.p |= mask;
                }
                if self.emulation {
                    self.p |= INDEX_8 | MEMORY_8;
                }
                if self.x8() {
                    self.x &= 0x00FF;
                    self.y &= 0x00FF;
                }
            }
            Op::Stp => self.stopped = true,
            Op::Wai => self.waiting = true,
            Op::Tax => {
                let value = self.c & acc_mask;
                self.store_x(value);
            }
            Op::Tay => {
                let value = self.c & acc_mask;
                self.store_y(value);
            }
            Op::Txa => {
                let value = self.x;
                self.set_acc(value);
            }
            Op::Tya => {
                let value = self.y;
                self.set_acc(value);
            }
            Op::Txy => {
                let value = self.x;
                self.store_y(value);
            }
            Op::Tyx => {
                let value = self.y;
                self.store_x(value);
            }
            Op::Tsx => {
                let value = self.s;
                self.store_x(value);
            }
            Op::Txs => {
                self.s = if self.x8() { 0x0100 | (self.x & 0xFF) } else { self.x };
            }
            Op::Tcd => {
                self.d = self.c;
                self.set_zn16(self.d);
                if self.emulation {
                    self.d = 0;
                }
            }
            Op::Tdc => {
                let value = self.d;
                self.set_acc(value);
            }
            Op::Tcs => {
                self.s = self.c;
                if self.emulation {
                    self.s = 0x0100 | (self.s & 0xFF);
                }
            }
            Op::Tsc => {
                let value = self.s;
                self.set_acc(value);
            }
            Op::Xba => {
                let low = self.c & 0xFF;
                self.c = (self.c >> 8) | (low << 8);
                self.set_zn8(self.c as u8);
            }
            Op::Xce => self.exchange_emulation(),
        }

        // An indexed read with a sixteen-bit index always takes the extra
        // cycle; with an eight-bit index it takes one only across a page.
        if matches!(
            op,
            Op::Adc | Op::And | Op::Bit | Op::Cmp | Op::Eor | Op::Lda | Op::Ldx | Op::Ldy | Op::Ora | Op::Sbc
        ) && matches!(
            mode,
            Mode::AbsX | Mode::AbsY | Mode::DpX | Mode::DpY | Mode::DpIndY | Mode::SrSY
        ) && (!self.x8() || effective.crossed_page)
        {
            cycles += 1;
        }

        self.cycles_total += cycles.max(0) as u64;
        cycles.max(0) as u32
    }

    fn operand(&mut self, bus: &mut dyn Bus, mode: Mode, ea: u32) -> u16 {
        if mode == Mode::ImmM {
            self.fetch_operand_data(bus)
        } else {
            self.read_data(bus, ea)
        }
    }
}


#[cfg(test)]
mod tests;
