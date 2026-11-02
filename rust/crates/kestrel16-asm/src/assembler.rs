// SPDX-License-Identifier: MIT
//
// The layout passes: statements in, a filled image and its symbol table out.
//
// The original numbers its passes by iterating to a fixed point. A pass
// evaluates every statement with the symbol values the previous pass produced,
// so a forward reference is known one pass later; a pass is accepted once two
// consecutive passes agree on every symbol. Only then does a final pass emit the
// bytes, this time refusing unknown symbols. That is what lets `LDA COUNT` pick
// the direct page without a hint when `COUNT` is defined further down, and it
// cannot oscillate the way a single two-pass guess can: an instruction that
// changes size changes the addresses the next pass sees, so the loop simply
// runs again.
//
// The width of an instruction is decided here rather than in the parser,
// because it depends on the value: a direct-page operand that will not fit a
// byte widens to absolute, and an absolute one that leaves the bank widens to
// long, as far as the mnemonic allows.

use std::collections::BTreeMap;

use crate::error::{Error, Result};
use crate::expr::{evaluate, Expr, SymbolTable};
use crate::opcode::{find_opcode, op_supports, Mode};
use crate::source::{Statement, StmtKind};

#[derive(Clone, Copy)]
pub struct Options {
    pub origin: u32,
    pub size: u32,
    pub fill: u8,
}

impl Default for Options {
    fn default() -> Self {
        Self {
            origin: 0,
            size: 0,
            fill: 0xFF,
        }
    }
}

#[derive(Clone, Debug)]
pub struct ListingEntry {
    pub address: u32,
    pub end: u32,
    pub file: String,
    pub line: usize,
    pub text: Vec<u8>,
}

/// What one pass knows: where it is, what it has named, and what it has emitted.
#[derive(Debug, Default)]
struct Layout {
    pc: u32,
    symbols: SymbolTable,
    image: BTreeMap<u32, u8>,
    listing: Vec<ListingEntry>,
}

pub struct Assembly {
    pub image: Vec<u8>,
    pub symbols: SymbolTable,
    pub listing: Vec<ListingEntry>,
}

pub struct Assembler {
    statements: Vec<Statement>,
    options: Options,
    entry_name: String,
}

impl Assembler {
    pub fn new(statements: Vec<Statement>, options: Options, entry_name: String) -> Self {
        Self {
            statements,
            options,
            entry_name,
        }
    }

    pub fn assemble(&self) -> Result<Assembly> {
        let mut previous = Layout::default();
        for iteration in 0..16 {
            let mut current = Layout::default();
            self.run_pass(&previous, &mut current, false, false)?;
            if iteration > 0 && current.symbols == previous.symbols {
                let mut settled = Layout::default();
                self.run_pass(&current, &mut settled, true, true)?;
                return Ok(Assembly {
                    image: self.finish(&settled),
                    symbols: settled.symbols,
                    listing: settled.listing,
                });
            }
            previous = current;
        }
        Err(Error::new(
            &self.entry_name,
            0,
            "layout did not converge -- a label is flipping between zero page and absolute addressing",
        ))
    }

    fn run_pass(
        &self,
        previous: &Layout,
        current: &mut Layout,
        emit: bool,
        strict: bool,
    ) -> Result<()> {
        current.pc = self.options.origin;
        for stmt in &self.statements {
            let start = current.pc;
            // A label takes the address the statement starts at. An equate
            // defines its own value, and an `.org` with an operand has already
            // moved on by the time anything could be said about it.
            let names_a_label = !stmt.label.is_empty()
                && stmt.kind != StmtKind::Equate
                && (stmt.kind != StmtKind::Org || stmt.operand.is_none());
            if names_a_label {
                define(current, stmt, start)?;
            }
            match stmt.kind {
                StmtKind::Instruction => {
                    self.assemble_instruction(stmt, previous, current, emit, strict)?
                }
                StmtKind::Equate => {
                    if let Some(operand) = &stmt.operand {
                        let value = self.value_of(operand, stmt, previous, current, strict)?;
                        current.symbols.insert(stmt.label.clone(), value);
                    }
                }
                StmtKind::Org => {
                    if let Some(operand) = &stmt.operand {
                        let where_to = self.value_of(operand, stmt, previous, current, strict)?;
                        if !(0..=0xFF_FFFF).contains(&where_to) {
                            return Err(Error::new(
                                &stmt.file,
                                stmt.line,
                                ".org outside the 16MB address space",
                            ));
                        }
                        if (where_to as u32) < current.pc {
                            return Err(Error::new(&stmt.file, stmt.line, ".org moves backwards"));
                        }
                        current.pc = where_to as u32;
                    }
                }
                StmtKind::Byte => {
                    for expr in &stmt.values {
                        let value = self.value_of(expr, stmt, previous, current, strict)?;
                        if !(-128..=255).contains(&value) {
                            return Err(Error::new(
                                &stmt.file,
                                stmt.line,
                                ".byte value does not fit in a byte",
                            ));
                        }
                        self.emit_byte(current, stmt, emit, (value & 0xFF) as u8)?;
                    }
                }
                StmtKind::Word => {
                    for expr in &stmt.values {
                        let value = self.value_of(expr, stmt, previous, current, strict)?;
                        if !(-32768..=0xFFFF).contains(&value) {
                            return Err(Error::new(
                                &stmt.file,
                                stmt.line,
                                ".word value does not fit in a word",
                            ));
                        }
                        self.emit_byte(current, stmt, emit, (value & 0xFF) as u8)?;
                        self.emit_byte(current, stmt, emit, ((value >> 8) & 0xFF) as u8)?;
                    }
                }
                StmtKind::Fill | StmtKind::Reserve | StmtKind::Align => {
                    let (count, fill_byte) = self.fill_count(stmt, previous, current, strict)?;
                    if count < 0 {
                        return Err(Error::new(
                            &stmt.file,
                            stmt.line,
                            "negative fill or reserve count",
                        ));
                    }
                    for _ in 0..count {
                        self.emit_byte(current, stmt, emit, fill_byte)?;
                    }
                }
                StmtKind::Text => {
                    for byte in &stmt.text {
                        self.emit_byte(current, stmt, emit, *byte)?;
                    }
                }
                StmtKind::Include => {}
            }
            if emit {
                current.listing.push(ListingEntry {
                    address: start,
                    end: current.pc,
                    file: stmt.file.clone(),
                    line: stmt.line,
                    text: stmt.raw.clone(),
                });
            }
        }
        Ok(())
    }

    /// How many bytes a `.fill`, `.res` or `.align` emits, and what they are
    /// filled with. An `.align` without a byte fills with zero, which is what
    /// the original does.
    fn fill_count(
        &self,
        stmt: &Statement,
        previous: &Layout,
        current: &Layout,
        strict: bool,
    ) -> Result<(i64, u8)> {
        let operand = stmt.operand.as_ref();
        let fill_value = stmt.fill_value.as_ref();
        match stmt.kind {
            StmtKind::Fill => {
                let count = self.value_of(
                    operand.expect("the parser gives .fill a count"),
                    stmt,
                    previous,
                    current,
                    strict,
                )?;
                let byte = self.value_of(
                    fill_value.expect("the parser gives .fill a byte"),
                    stmt,
                    previous,
                    current,
                    strict,
                )?;
                Ok((count, (byte & 0xFF) as u8))
            }
            StmtKind::Reserve => {
                let count = self.value_of(
                    operand.expect("the parser gives .res a count"),
                    stmt,
                    previous,
                    current,
                    strict,
                )?;
                Ok((count, 0))
            }
            _ => {
                let alignment = self.value_of(
                    operand.expect("the parser gives .align a boundary"),
                    stmt,
                    previous,
                    current,
                    strict,
                )?;
                if alignment <= 0 {
                    return Err(Error::new(
                        &stmt.file,
                        stmt.line,
                        ".align needs a positive boundary",
                    ));
                }
                let remainder = i64::from(current.pc) % alignment;
                let count = if remainder == 0 {
                    0
                } else {
                    alignment - remainder
                };
                let byte = match fill_value {
                    Some(expr) => {
                        (self.value_of(expr, stmt, previous, current, strict)? & 0xFF) as u8
                    }
                    None => 0,
                };
                Ok((count, byte))
            }
        }
    }

    fn value_of(
        &self,
        node: &Expr,
        stmt: &Statement,
        previous: &Layout,
        current: &Layout,
        strict: bool,
    ) -> Result<i64> {
        // Labels defined so far in this pass win over the previous pass, so a
        // backward reference is already exact. The original merges the two
        // tables into a fresh one for every statement; looking each name up in
        // turn asks the same question without copying the symbol table.
        let lookup = |name: &str| {
            current
                .symbols
                .get(name)
                .or_else(|| previous.symbols.get(name))
                .copied()
        };
        evaluate(
            node,
            &lookup,
            i64::from(current.pc),
            &stmt.file,
            stmt.line,
            !strict,
        )
    }

    fn emit_byte(
        &self,
        layout: &mut Layout,
        stmt: &Statement,
        emit: bool,
        value: u8,
    ) -> Result<()> {
        let address = layout.pc;
        if self.options.size != 0 {
            let end = self.options.origin.wrapping_add(self.options.size);
            if address < self.options.origin || address >= end {
                return Err(Error::new(
                    &stmt.file,
                    stmt.line,
                    format!(
                        "code falls outside the ROM window ${}..=${}",
                        hex(self.options.origin),
                        hex(end.wrapping_sub(1))
                    ),
                ));
            }
        }
        if address > 0xFF_FFFF {
            return Err(Error::new(&stmt.file, stmt.line, "past the end of memory"));
        }
        if emit {
            layout.image.insert(address, value);
        }
        layout.pc += 1;
        Ok(())
    }

    fn assemble_instruction(
        &self,
        stmt: &Statement,
        previous: &Layout,
        current: &mut Layout,
        emit: bool,
        strict: bool,
    ) -> Result<()> {
        let mut mode = stmt.mode;
        let value = match &stmt.operand {
            Some(operand) => self.value_of(operand, stmt, previous, current, strict)?,
            None => 0,
        };
        // An operand that will not fit its short form is widened as far as the
        // mnemonic allows: direct page to absolute to long.
        if stmt.long_form {
            if matches!(mode, Mode::Dp | Mode::Abs) && op_supports(&stmt.mnemonic, Mode::Long) {
                mode = Mode::Long;
            } else if matches!(mode, Mode::DpX | Mode::AbsX)
                && op_supports(&stmt.mnemonic, Mode::LongX)
            {
                mode = Mode::LongX;
            }
        }
        let too_wide = !(0..=0xFF).contains(&value);
        let past_bank = !(0..=0xFFFF).contains(&value);
        if mode == Mode::DpX && too_wide && op_supports(&stmt.mnemonic, Mode::AbsX) {
            mode = Mode::AbsX;
        } else if mode == Mode::DpY && too_wide && op_supports(&stmt.mnemonic, Mode::AbsY) {
            mode = Mode::AbsY;
        } else if mode == Mode::Dp && too_wide && op_supports(&stmt.mnemonic, Mode::Abs) {
            mode = Mode::Abs;
        } else if mode == Mode::DpLong && too_wide && op_supports(&stmt.mnemonic, Mode::AbsIndLong)
        {
            mode = Mode::AbsIndLong;
        } else if mode == Mode::DpInd && too_wide && op_supports(&stmt.mnemonic, Mode::AbsInd) {
            mode = Mode::AbsInd;
        }
        if matches!(mode, Mode::Abs | Mode::AbsX) && past_bank {
            if mode == Mode::Abs && op_supports(&stmt.mnemonic, Mode::Long) {
                mode = Mode::Long;
            } else if mode == Mode::AbsX && op_supports(&stmt.mnemonic, Mode::LongX) {
                mode = Mode::LongX;
            }
        }
        let Some(op) = find_opcode(&stmt.mnemonic, mode) else {
            return Err(Error::new(
                &stmt.file,
                stmt.line,
                format!("{} does not support {}", stmt.mnemonic, mode),
            ));
        };
        let at = current.pc;
        if mode == Mode::Rel {
            // A branch that cannot reach its target becomes its own inverse
            // branch over a JMP. The pair of branch opcodes differs in bit 5
            // (BCC $90 / BCS $B0, BNE $D0 / BEQ $F0, and so on), so the inverse
            // is one XOR -- which is what makes a long branch possible without a
            // macro or a rewrite of the source.
            let offset = value - i64::from(at + 2);
            if (-128..=127).contains(&offset) {
                self.emit_byte(current, stmt, emit, op.code)?;
                self.emit_byte(current, stmt, emit, (offset & 0xFF) as u8)?;
            } else if op.code == 0x80 {
                // BRA is the one branch with no inverse: $80 ^ $20 is $A0,
                // which is LDY, not a branch. A BRA that cannot reach is a JMP.
                self.emit_byte(current, stmt, emit, 0x4C)?;
                self.emit_byte(current, stmt, emit, (value & 0xFF) as u8)?;
                self.emit_byte(current, stmt, emit, ((value >> 8) & 0xFF) as u8)?;
            } else {
                self.emit_byte(current, stmt, emit, op.code ^ 0x20)?;
                self.emit_byte(current, stmt, emit, 3)?;
                self.emit_byte(current, stmt, emit, 0x4C)?;
                self.emit_byte(current, stmt, emit, (value & 0xFF) as u8)?;
                self.emit_byte(current, stmt, emit, ((value >> 8) & 0xFF) as u8)?;
            }
            return Ok(());
        }
        if mode == Mode::Rel16 {
            // BRL and PER reach anywhere in the bank: the displacement is
            // sixteen bits and counts from the end of the instruction.
            let offset = value - i64::from(at + 3);
            self.emit_byte(current, stmt, emit, op.code)?;
            self.emit_byte(current, stmt, emit, (offset & 0xFF) as u8)?;
            self.emit_byte(current, stmt, emit, ((offset >> 8) & 0xFF) as u8)?;
            return Ok(());
        }
        self.emit_byte(current, stmt, emit, op.code)?;
        match mode {
            Mode::Imp | Mode::Acc => Ok(()),
            Mode::Imp8 | Mode::Imm8 => self.emit_byte(current, stmt, emit, (value & 0xFF) as u8),
            Mode::Imm16 => {
                self.emit_byte(current, stmt, emit, (value & 0xFF) as u8)?;
                self.emit_byte(current, stmt, emit, ((value >> 8) & 0xFF) as u8)
            }
            Mode::Imm => {
                if stmt.imm_bytes == 1 && !(-128..=0xFF).contains(&value) {
                    // A byte immediate that does not fit is a missing `.W`: the
                    // assembler cannot see the processor's M flag, so the source
                    // has to say when two bytes are meant. Emitting the low byte
                    // and carrying on would silently eat the next opcode.
                    return Err(Error::new(
                        &stmt.file,
                        stmt.line,
                        format!("{}: immediate does not fit a byte; use .W", stmt.mnemonic),
                    ));
                }
                self.emit_byte(current, stmt, emit, (value & 0xFF) as u8)?;
                if stmt.imm_bytes == 1 {
                    return Ok(());
                }
                self.emit_byte(current, stmt, emit, ((value >> 8) & 0xFF) as u8)
            }
            Mode::Dp
            | Mode::DpX
            | Mode::DpY
            | Mode::DpInd
            | Mode::DpIndX
            | Mode::DpIndY
            | Mode::DpLong
            | Mode::DpLongY
            | Mode::SrS
            | Mode::SrSY => self.emit_byte(current, stmt, emit, (value & 0xFF) as u8),
            Mode::Blk => {
                // The value is dst * 256 + src, and the opcode wants the
                // destination bank first.
                self.emit_byte(current, stmt, emit, ((value >> 8) & 0xFF) as u8)?;
                self.emit_byte(current, stmt, emit, (value & 0xFF) as u8)
            }
            Mode::Abs
            | Mode::AbsX
            | Mode::AbsY
            | Mode::AbsInd
            | Mode::AbsIndX
            | Mode::AbsIndLong => {
                self.emit_byte(current, stmt, emit, (value & 0xFF) as u8)?;
                self.emit_byte(current, stmt, emit, ((value >> 8) & 0xFF) as u8)
            }
            Mode::Long | Mode::LongX => {
                self.emit_byte(current, stmt, emit, (value & 0xFF) as u8)?;
                self.emit_byte(current, stmt, emit, ((value >> 8) & 0xFF) as u8)?;
                self.emit_byte(current, stmt, emit, ((value >> 16) & 0xFF) as u8)
            }
            Mode::Rel | Mode::Rel16 => unreachable!("the relative modes return above"),
        }
    }

    fn finish(&self, layout: &Layout) -> Vec<u8> {
        let base = if self.options.size != 0 {
            self.options.origin
        } else if let Some((first, _)) = layout.image.iter().next() {
            *first
        } else {
            self.options.origin
        };
        if self.options.size == 0 {
            let Some((last, _)) = layout.image.iter().next_back() else {
                return Vec::new();
            };
            let mut out = vec![self.options.fill; (last - base + 1) as usize];
            for (address, byte) in &layout.image {
                out[(address - base) as usize] = *byte;
            }
            return out;
        }
        let mut out = vec![self.options.fill; self.options.size as usize];
        for (address, byte) in &layout.image {
            if *address < base || *address >= base.wrapping_add(self.options.size) {
                continue;
            }
            out[(address - base) as usize] = *byte;
        }
        out
    }
}

fn define(layout: &mut Layout, stmt: &Statement, address: u32) -> Result<()> {
    if layout.symbols.contains_key(&stmt.label) {
        return Err(Error::new(
            &stmt.file,
            stmt.line,
            format!("label defined twice: {}", stmt.label),
        ));
    }
    layout
        .symbols
        .insert(stmt.label.clone(), i64::from(address));
    Ok(())
}

/// Four hex digits, which is the width of an address in the ROM window message.
fn hex(value: u32) -> String {
    format!("{value:04X}")
}
