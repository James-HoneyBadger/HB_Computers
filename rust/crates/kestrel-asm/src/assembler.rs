// SPDX-License-Identifier: MIT
//
// The layout passes: statements in, a filled image and its symbol table out.
//
// The original numbers its passes by iterating to a fixed point. A pass
// evaluates every statement with the symbol values the previous pass produced,
// so a forward reference is known one pass later; a pass is accepted once two
// consecutive passes agree on every symbol. Only then does a final pass emit
// the bytes, this time refusing unknown symbols. That is what lets `LDA COUNT`
// pick zero page without a hint when `COUNT` is defined further down, and it
// cannot oscillate the way a single two-pass guess can: an instruction that
// changes size changes the addresses the next pass sees, so the loop simply
// runs again.

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
                        if !(0..=0xFFFF).contains(&where_to) {
                            return Err(Error::new(
                                &stmt.file,
                                stmt.line,
                                ".org outside the 64K address space",
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
                    let (count, fill_byte) = match stmt.kind {
                        StmtKind::Fill => {
                            let count = self.value_of(
                                stmt.operand
                                    .as_ref()
                                    .expect("the parser gives .fill a count"),
                                stmt,
                                previous,
                                current,
                                strict,
                            )?;
                            let byte = self.value_of(
                                stmt.fill_value
                                    .as_ref()
                                    .expect("the parser gives .fill a byte"),
                                stmt,
                                previous,
                                current,
                                strict,
                            )?;
                            (count, byte as u8)
                        }
                        StmtKind::Reserve => {
                            let count = self.value_of(
                                stmt.operand
                                    .as_ref()
                                    .expect("the parser gives .res a count"),
                                stmt,
                                previous,
                                current,
                                strict,
                            )?;
                            (count, 0)
                        }
                        _ => {
                            let alignment = self.value_of(
                                stmt.operand
                                    .as_ref()
                                    .expect("the parser gives .align a boundary"),
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
                            let byte = match &stmt.fill_value {
                                Some(fill_value) => self
                                    .value_of(fill_value, stmt, previous, current, strict)?
                                    as u8,
                                None => 0,
                            };
                            (count, byte)
                        }
                    };
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
            let last = self
                .options
                .origin
                .wrapping_add(self.options.size)
                .wrapping_sub(1);
            if address < self.options.origin || address > last {
                return Err(Error::new(
                    &stmt.file,
                    stmt.line,
                    format!(
                        "code falls outside the ROM window ${}..=${}",
                        hex(self.options.origin),
                        hex(last)
                    ),
                ));
            }
        }
        if address > 0xFFFF {
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
        let mut value = 0i64;
        if let Some(operand) = &stmt.operand {
            value = self.value_of(operand, stmt, previous, current, strict)?;
        }
        // A zero-page form is only chosen up front; the value decides final
        // size, and a large one falls back to the absolute form.
        if mode == Mode::ZpX
            && !(0..=0xFF).contains(&value)
            && op_supports(&stmt.mnemonic, Mode::AbsX)
        {
            mode = Mode::AbsX;
        } else if mode == Mode::ZpY
            && !(0..=0xFF).contains(&value)
            && op_supports(&stmt.mnemonic, Mode::AbsY)
        {
            mode = Mode::AbsY;
        } else if mode == Mode::Zp
            && !(0..=0xFF).contains(&value)
            && op_supports(&stmt.mnemonic, Mode::Abs)
        {
            mode = Mode::Abs;
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
        // An operand that does not fit the form it was given is an error, not a
        // truncation: `STX $1234,Y` has no absolute,Y form, and emitting the low
        // byte would assemble a program that does something else.
        let fits = match mode {
            Mode::Imp | Mode::Acc => value == 0 || stmt.operand.is_none(),
            Mode::Imm | Mode::Zp | Mode::ZpX | Mode::ZpY | Mode::IndX | Mode::IndY => {
                (0..=0xFF).contains(&value)
            }
            _ => (0..=0xFFFF).contains(&value),
        };
        if !fits {
            return Err(Error::new(
                &stmt.file,
                stmt.line,
                format!("{}: {value} does not fit {}", stmt.mnemonic, mode),
            ));
        }
        self.emit_byte(current, stmt, emit, op.code)?;
        match mode {
            Mode::Imp | Mode::Acc => Ok(()),
            Mode::Imm | Mode::Zp | Mode::ZpX | Mode::ZpY | Mode::IndX | Mode::IndY => {
                self.emit_byte(current, stmt, emit, (value & 0xFF) as u8)
            }
            _ => {
                self.emit_byte(current, stmt, emit, (value & 0xFF) as u8)?;
                self.emit_byte(current, stmt, emit, ((value >> 8) & 0xFF) as u8)
            }
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

/// Four hex digits, which is the width of an address on this machine.
fn hex(value: u32) -> String {
    format!("{value:04X}")
}
