// SPDX-License-Identifier: MIT
//
// The instruction set: every documented 6502 opcode, with the addressing mode
// each one takes. Undocumented opcodes are not assembled: a ROM that needs one
// emits `.byte` and says why in a comment.
//
// A mnemonic appears once per mode it supports, which is what makes mode
// selection a lookup rather than a table of bit patterns, and what decides, for
// example, that `LDA $1234,Y` has an absolute,Y form but `LDY $1234,Y` does
// not.

use std::fmt;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Mode {
    Imp,
    Acc,
    Imm,
    Zp,
    ZpX,
    ZpY,
    Abs,
    AbsX,
    AbsY,
    Ind,
    IndX,
    IndY,
    Rel,
}

impl Mode {
    /// The mode as a diagnostic names it, so an unsupported form says which one
    /// it was.
    pub fn name(self) -> &'static str {
        match self {
            Mode::Imp => "implied",
            Mode::Acc => "accumulator",
            Mode::Imm => "immediate",
            Mode::Zp => "zero page",
            Mode::ZpX => "zero page,X",
            Mode::ZpY => "zero page,Y",
            Mode::Abs => "absolute",
            Mode::AbsX => "absolute,X",
            Mode::AbsY => "absolute,Y",
            Mode::Ind => "indirect",
            Mode::IndX => "(indirect,X)",
            Mode::IndY => "(indirect),Y",
            Mode::Rel => "relative",
        }
    }
}

impl fmt::Display for Mode {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.name())
    }
}

pub struct OpDef {
    pub name: &'static str,
    pub mode: Mode,
    pub code: u8,
}

/// Every documented opcode, in the order the original table lists them.
#[rustfmt::skip]
pub const OPCODES: &[OpDef] = &[
    OpDef { name: "ADC", mode: Mode::Imm, code: 0x69 },
    OpDef { name: "ADC", mode: Mode::Zp, code: 0x65 },
    OpDef { name: "ADC", mode: Mode::ZpX, code: 0x75 },
    OpDef { name: "ADC", mode: Mode::Abs, code: 0x6D },
    OpDef { name: "ADC", mode: Mode::AbsX, code: 0x7D },
    OpDef { name: "ADC", mode: Mode::AbsY, code: 0x79 },
    OpDef { name: "ADC", mode: Mode::IndX, code: 0x61 },
    OpDef { name: "ADC", mode: Mode::IndY, code: 0x71 },
    OpDef { name: "AND", mode: Mode::Imm, code: 0x29 },
    OpDef { name: "AND", mode: Mode::Zp, code: 0x25 },
    OpDef { name: "AND", mode: Mode::ZpX, code: 0x35 },
    OpDef { name: "AND", mode: Mode::Abs, code: 0x2D },
    OpDef { name: "AND", mode: Mode::AbsX, code: 0x3D },
    OpDef { name: "AND", mode: Mode::AbsY, code: 0x39 },
    OpDef { name: "AND", mode: Mode::IndX, code: 0x21 },
    OpDef { name: "AND", mode: Mode::IndY, code: 0x31 },
    OpDef { name: "ASL", mode: Mode::Acc, code: 0x0A },
    OpDef { name: "ASL", mode: Mode::Zp, code: 0x06 },
    OpDef { name: "ASL", mode: Mode::ZpX, code: 0x16 },
    OpDef { name: "ASL", mode: Mode::Abs, code: 0x0E },
    OpDef { name: "ASL", mode: Mode::AbsX, code: 0x1E },
    OpDef { name: "BCC", mode: Mode::Rel, code: 0x90 },
    OpDef { name: "BCS", mode: Mode::Rel, code: 0xB0 },
    OpDef { name: "BEQ", mode: Mode::Rel, code: 0xF0 },
    OpDef { name: "BMI", mode: Mode::Rel, code: 0x30 },
    OpDef { name: "BNE", mode: Mode::Rel, code: 0xD0 },
    OpDef { name: "BPL", mode: Mode::Rel, code: 0x10 },
    OpDef { name: "BVC", mode: Mode::Rel, code: 0x50 },
    OpDef { name: "BVS", mode: Mode::Rel, code: 0x70 },
    OpDef { name: "BIT", mode: Mode::Zp, code: 0x24 },
    OpDef { name: "BIT", mode: Mode::Abs, code: 0x2C },
    OpDef { name: "BRK", mode: Mode::Imp, code: 0x00 },
    OpDef { name: "CLC", mode: Mode::Imp, code: 0x18 },
    OpDef { name: "CLD", mode: Mode::Imp, code: 0xD8 },
    OpDef { name: "CLI", mode: Mode::Imp, code: 0x58 },
    OpDef { name: "CLV", mode: Mode::Imp, code: 0xB8 },
    OpDef { name: "CMP", mode: Mode::Imm, code: 0xC9 },
    OpDef { name: "CMP", mode: Mode::Zp, code: 0xC5 },
    OpDef { name: "CMP", mode: Mode::ZpX, code: 0xD5 },
    OpDef { name: "CMP", mode: Mode::Abs, code: 0xCD },
    OpDef { name: "CMP", mode: Mode::AbsX, code: 0xDD },
    OpDef { name: "CMP", mode: Mode::AbsY, code: 0xD9 },
    OpDef { name: "CMP", mode: Mode::IndX, code: 0xC1 },
    OpDef { name: "CMP", mode: Mode::IndY, code: 0xD1 },
    OpDef { name: "CPX", mode: Mode::Imm, code: 0xE0 },
    OpDef { name: "CPX", mode: Mode::Zp, code: 0xE4 },
    OpDef { name: "CPX", mode: Mode::Abs, code: 0xEC },
    OpDef { name: "CPY", mode: Mode::Imm, code: 0xC0 },
    OpDef { name: "CPY", mode: Mode::Zp, code: 0xC4 },
    OpDef { name: "CPY", mode: Mode::Abs, code: 0xCC },
    OpDef { name: "DEC", mode: Mode::Zp, code: 0xC6 },
    OpDef { name: "DEC", mode: Mode::ZpX, code: 0xD6 },
    OpDef { name: "DEC", mode: Mode::Abs, code: 0xCE },
    OpDef { name: "DEC", mode: Mode::AbsX, code: 0xDE },
    OpDef { name: "DEX", mode: Mode::Imp, code: 0xCA },
    OpDef { name: "DEY", mode: Mode::Imp, code: 0x88 },
    OpDef { name: "EOR", mode: Mode::Imm, code: 0x49 },
    OpDef { name: "EOR", mode: Mode::Zp, code: 0x45 },
    OpDef { name: "EOR", mode: Mode::ZpX, code: 0x55 },
    OpDef { name: "EOR", mode: Mode::Abs, code: 0x4D },
    OpDef { name: "EOR", mode: Mode::AbsX, code: 0x5D },
    OpDef { name: "EOR", mode: Mode::AbsY, code: 0x59 },
    OpDef { name: "EOR", mode: Mode::IndX, code: 0x41 },
    OpDef { name: "EOR", mode: Mode::IndY, code: 0x51 },
    OpDef { name: "INC", mode: Mode::Zp, code: 0xE6 },
    OpDef { name: "INC", mode: Mode::ZpX, code: 0xF6 },
    OpDef { name: "INC", mode: Mode::Abs, code: 0xEE },
    OpDef { name: "INC", mode: Mode::AbsX, code: 0xFE },
    OpDef { name: "INX", mode: Mode::Imp, code: 0xE8 },
    OpDef { name: "INY", mode: Mode::Imp, code: 0xC8 },
    OpDef { name: "JMP", mode: Mode::Abs, code: 0x4C },
    OpDef { name: "JMP", mode: Mode::Ind, code: 0x6C },
    OpDef { name: "JSR", mode: Mode::Abs, code: 0x20 },
    OpDef { name: "LDA", mode: Mode::Imm, code: 0xA9 },
    OpDef { name: "LDA", mode: Mode::Zp, code: 0xA5 },
    OpDef { name: "LDA", mode: Mode::ZpX, code: 0xB5 },
    OpDef { name: "LDA", mode: Mode::Abs, code: 0xAD },
    OpDef { name: "LDA", mode: Mode::AbsX, code: 0xBD },
    OpDef { name: "LDA", mode: Mode::AbsY, code: 0xB9 },
    OpDef { name: "LDA", mode: Mode::IndX, code: 0xA1 },
    OpDef { name: "LDA", mode: Mode::IndY, code: 0xB1 },
    OpDef { name: "LDX", mode: Mode::Imm, code: 0xA2 },
    OpDef { name: "LDX", mode: Mode::Zp, code: 0xA6 },
    OpDef { name: "LDX", mode: Mode::ZpY, code: 0xB6 },
    OpDef { name: "LDX", mode: Mode::Abs, code: 0xAE },
    OpDef { name: "LDX", mode: Mode::AbsY, code: 0xBE },
    OpDef { name: "LDY", mode: Mode::Imm, code: 0xA0 },
    OpDef { name: "LDY", mode: Mode::Zp, code: 0xA4 },
    OpDef { name: "LDY", mode: Mode::ZpX, code: 0xB4 },
    OpDef { name: "LDY", mode: Mode::Abs, code: 0xAC },
    OpDef { name: "LDY", mode: Mode::AbsX, code: 0xBC },
    OpDef { name: "LSR", mode: Mode::Acc, code: 0x4A },
    OpDef { name: "LSR", mode: Mode::Zp, code: 0x46 },
    OpDef { name: "LSR", mode: Mode::ZpX, code: 0x56 },
    OpDef { name: "LSR", mode: Mode::Abs, code: 0x4E },
    OpDef { name: "LSR", mode: Mode::AbsX, code: 0x5E },
    OpDef { name: "NOP", mode: Mode::Imp, code: 0xEA },
    OpDef { name: "ORA", mode: Mode::Imm, code: 0x09 },
    OpDef { name: "ORA", mode: Mode::Zp, code: 0x05 },
    OpDef { name: "ORA", mode: Mode::ZpX, code: 0x15 },
    OpDef { name: "ORA", mode: Mode::Abs, code: 0x0D },
    OpDef { name: "ORA", mode: Mode::AbsX, code: 0x1D },
    OpDef { name: "ORA", mode: Mode::AbsY, code: 0x19 },
    OpDef { name: "ORA", mode: Mode::IndX, code: 0x01 },
    OpDef { name: "ORA", mode: Mode::IndY, code: 0x11 },
    OpDef { name: "PHA", mode: Mode::Imp, code: 0x48 },
    OpDef { name: "PHP", mode: Mode::Imp, code: 0x08 },
    OpDef { name: "PLA", mode: Mode::Imp, code: 0x68 },
    OpDef { name: "PLP", mode: Mode::Imp, code: 0x28 },
    OpDef { name: "ROL", mode: Mode::Acc, code: 0x2A },
    OpDef { name: "ROL", mode: Mode::Zp, code: 0x26 },
    OpDef { name: "ROL", mode: Mode::ZpX, code: 0x36 },
    OpDef { name: "ROL", mode: Mode::Abs, code: 0x2E },
    OpDef { name: "ROL", mode: Mode::AbsX, code: 0x3E },
    OpDef { name: "ROR", mode: Mode::Acc, code: 0x6A },
    OpDef { name: "ROR", mode: Mode::Zp, code: 0x66 },
    OpDef { name: "ROR", mode: Mode::ZpX, code: 0x76 },
    OpDef { name: "ROR", mode: Mode::Abs, code: 0x6E },
    OpDef { name: "ROR", mode: Mode::AbsX, code: 0x7E },
    OpDef { name: "RTI", mode: Mode::Imp, code: 0x40 },
    OpDef { name: "RTS", mode: Mode::Imp, code: 0x60 },
    OpDef { name: "SBC", mode: Mode::Imm, code: 0xE9 },
    OpDef { name: "SBC", mode: Mode::Zp, code: 0xE5 },
    OpDef { name: "SBC", mode: Mode::ZpX, code: 0xF5 },
    OpDef { name: "SBC", mode: Mode::Abs, code: 0xED },
    OpDef { name: "SBC", mode: Mode::AbsX, code: 0xFD },
    OpDef { name: "SBC", mode: Mode::AbsY, code: 0xF9 },
    OpDef { name: "SBC", mode: Mode::IndX, code: 0xE1 },
    OpDef { name: "SBC", mode: Mode::IndY, code: 0xF1 },
    OpDef { name: "SEC", mode: Mode::Imp, code: 0x38 },
    OpDef { name: "SED", mode: Mode::Imp, code: 0xF8 },
    OpDef { name: "SEI", mode: Mode::Imp, code: 0x78 },
    OpDef { name: "STA", mode: Mode::Zp, code: 0x85 },
    OpDef { name: "STA", mode: Mode::ZpX, code: 0x95 },
    OpDef { name: "STA", mode: Mode::Abs, code: 0x8D },
    OpDef { name: "STA", mode: Mode::AbsX, code: 0x9D },
    OpDef { name: "STA", mode: Mode::AbsY, code: 0x99 },
    OpDef { name: "STA", mode: Mode::IndX, code: 0x81 },
    OpDef { name: "STA", mode: Mode::IndY, code: 0x91 },
    OpDef { name: "STX", mode: Mode::Zp, code: 0x86 },
    OpDef { name: "STX", mode: Mode::ZpY, code: 0x96 },
    OpDef { name: "STX", mode: Mode::Abs, code: 0x8E },
    OpDef { name: "STY", mode: Mode::Zp, code: 0x84 },
    OpDef { name: "STY", mode: Mode::ZpX, code: 0x94 },
    OpDef { name: "STY", mode: Mode::Abs, code: 0x8C },
    OpDef { name: "TAX", mode: Mode::Imp, code: 0xAA },
    OpDef { name: "TAY", mode: Mode::Imp, code: 0xA8 },
    OpDef { name: "TSX", mode: Mode::Imp, code: 0xBA },
    OpDef { name: "TXA", mode: Mode::Imp, code: 0x8A },
    OpDef { name: "TXS", mode: Mode::Imp, code: 0x9A },
    OpDef { name: "TYA", mode: Mode::Imp, code: 0x98 },
];

pub fn find_opcode(name: &str, mode: Mode) -> Option<&'static OpDef> {
    OPCODES.iter().find(|op| op.name == name && op.mode == mode)
}

pub fn op_supports(name: &str, mode: Mode) -> bool {
    find_opcode(name, mode).is_some()
}

pub fn is_branch(name: &str) -> bool {
    op_supports(name, Mode::Rel)
}

pub fn is_mnemonic(name: &str) -> bool {
    OPCODES.iter().any(|op| op.name == name)
}
