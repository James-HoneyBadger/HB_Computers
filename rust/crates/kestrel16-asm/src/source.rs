// SPDX-License-Identifier: MIT
//
// The statement parser: source lines in, the flat list of statements the layout
// passes walk.
//
// The dialect it accepts is the one the Kestrel 16's ROMs are written in: a
// label at the current address (with or without a colon, in column 0 or not),
// `NAME = expr` for a constant, the handful of data directives, and the
// instructions with the width suffixes that decide how wide an immediate is.
//
// Includes are expanded in place, so the passes see one flat program: the
// included file's own `.org` and labels do the rest. A ROM is a single image, so
// an include is textual rather than a separate link.

use crate::error::{Error, Result};
use crate::expr::{BinOp, Expr, ExprParser};
use crate::opcode::{self, base_mnemonic, Mode};
use crate::token::{lex, Token};

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum StmtKind {
    Instruction,
    Equate,
    Org,
    Byte,
    Word,
    Fill,
    Reserve,
    Align,
    Text,
    Include,
}

#[derive(Debug)]
pub struct Statement {
    pub kind: StmtKind,
    pub label: String,
    pub file: String,
    pub line: usize,
    /// The line as written, which is what the listing shows beside the bytes.
    pub raw: Vec<u8>,

    pub mnemonic: String,
    pub mode: Mode,
    pub operand: Option<Expr>,
    /// The width of an immediate, which depends on the M or X flag at run time.
    /// A byte is the default, as it is on the 6502 this processor starts in;
    /// `.W` says the flag is clear and two bytes are wanted, `.B` says so again
    /// for a reader, and `.L` asks for a long address.
    pub imm_bytes: u8,
    /// True when the source said `.B`, `.W` or `.L`, which beats any default.
    pub imm_explicit: bool,
    /// `LDA.L` asks for the 24-bit form even when the value would fit.
    pub long_form: bool,

    pub values: Vec<Expr>,
    pub fill_value: Option<Expr>,
    pub text: Vec<u8>,
}

impl Statement {
    fn new(kind: StmtKind, file: &str, line: usize, raw: &[u8]) -> Self {
        Self {
            kind,
            label: String::new(),
            file: file.to_string(),
            line,
            raw: raw.to_vec(),
            mnemonic: String::new(),
            mode: Mode::Imp,
            operand: None,
            imm_bytes: 1,
            imm_explicit: false,
            long_form: false,
            values: Vec::new(),
            fill_value: None,
            text: Vec::new(),
        }
    }
}

fn is_directive(name: &str) -> bool {
    matches!(
        name,
        ".ORG"
            | ".BYTE"
            | ".DB"
            | ".WORD"
            | ".DW"
            | ".FILL"
            | ".RES"
            | ".ALIGN"
            | ".TEXT"
            | ".INCLUDE"
            | ".ASCIIZ"
    )
}

/// A mnemonic with its width suffix taken off: the width the immediate should
/// carry, and whether the source asked for the long form.
fn split_suffix(head: &str) -> (&str, u8, bool) {
    let Some((base, suffix)) = head.rsplit_once('.') else {
        return (head, 1, false);
    };
    match suffix {
        "B" => (base, 1, false),
        "W" => (base, 2, false),
        "L" => (base, 1, true),
        _ => (head, 1, false),
    }
}

/// The instructions whose immediate is an index register's, which this machine
/// keeps sixteen bits wide.
fn is_index_immediate(name: &str) -> bool {
    matches!(name, "LDX" | "LDY" | "CPX" | "CPY")
}

/// `,X`, `,Y` or `,S` at `pos`.
fn looks_like_index(tokens: &[Token], pos: usize, register: &str) -> bool {
    tokens.get(pos).is_some_and(|token| token.is_punct(","))
        && tokens
            .get(pos + 1)
            .is_some_and(|token| token.is_ident(register))
}

pub struct SourceParser {
    depth: i32,
}

impl SourceParser {
    pub fn new(depth: i32) -> Self {
        Self { depth }
    }

    pub fn parse_file(&self, path: &str) -> Result<Vec<Statement>> {
        // The limit is applied as the file is read, so a cycle between two
        // includes stops rather than recursing until the stack runs out.
        if self.depth > 8 {
            return Err(Error::new(path, 0, "include nesting is too deep"));
        }
        let contents =
            std::fs::read(path).map_err(|_| Error::new(path, 0, "cannot open source file"))?;
        let mut statements = Vec::new();
        let mut number = 0;
        for line in contents.split(|byte| *byte == b'\n') {
            number += 1;
            self.append_line(path, number, line, &mut statements)?;
        }
        Ok(statements)
    }

    fn append_line(
        &self,
        file: &str,
        number: usize,
        raw: &[u8],
        out: &mut Vec<Statement>,
    ) -> Result<()> {
        let tokens = lex(raw, file, number)?;
        if tokens.is_empty() {
            return Ok(());
        }
        let mut pos = 0;
        let mut label = String::new();
        if tokens.len() >= 2 && matches!(tokens[0], Token::Ident(_)) && tokens[1].is_punct(":") {
            label = tokens[0].spelling_or_blank();
            pos = 2;
        } else if tokens.len() >= 2
            && matches!(tokens[0], Token::Ident(_))
            && tokens[1].is_punct("=")
        {
            let mut stmt = Statement::new(StmtKind::Equate, file, number, raw);
            stmt.label = tokens[0].spelling_or_blank();
            stmt.operand = Some(ExprParser::new(&tokens, 2, file, number).parse()?);
            out.push(stmt);
            return Ok(());
        }
        if label.is_empty() {
            if let Token::Ident(name) = &tokens[0] {
                // `LDA.B` is still an instruction, so the test is on the name
                // without its suffix; anything else in the label field is a
                // label whether or not it carries the colon.
                if !is_directive(name) && !opcode::is_mnemonic(base_mnemonic(name)) {
                    label = name.clone();
                    pos = 1;
                }
            }
        }
        if pos >= tokens.len() {
            if !label.is_empty() {
                // A label with nothing after it defines an address and emits
                // nothing, which is how a source names a constant address.
                let mut stmt = Statement::new(StmtKind::Org, file, number, raw);
                stmt.label = label;
                out.push(stmt);
            }
            return Ok(());
        }
        let Token::Ident(head) = &tokens[pos] else {
            return Err(Error::new(
                file,
                number,
                "expected a label, directive or mnemonic",
            ));
        };
        let suffixed = head.contains('.');
        let (name, imm_bytes, long_form) = split_suffix(head);
        if is_directive(name) {
            let mut stmt = Statement::new(StmtKind::Instruction, file, number, raw);
            stmt.label = label;
            self.parse_directive(name, &mut stmt, &tokens, pos + 1)?;
            out.push(stmt);
            return Ok(());
        }
        let mut stmt = Statement::new(StmtKind::Instruction, file, number, raw);
        stmt.label = label;
        stmt.mnemonic = name.to_string();
        stmt.imm_bytes = imm_bytes;
        stmt.imm_explicit = suffixed;
        stmt.long_form = long_form;
        if !opcode::is_mnemonic(name) {
            return Err(Error::new(
                file,
                number,
                format!("unknown mnemonic: {name}"),
            ));
        }
        self.parse_operand(&mut stmt, &tokens, pos + 1, file, number)?;
        out.push(stmt);
        Ok(())
    }

    fn parse_operand(
        &self,
        stmt: &mut Statement,
        tokens: &[Token],
        pos: usize,
        file: &str,
        line: usize,
    ) -> Result<()> {
        let name = stmt.mnemonic.clone();
        if pos >= tokens.len() {
            // `ASL` with nothing after it shifts the accumulator: the implied
            // form does not exist for the shift mnemonics. REP and SEP look
            // implied but always carry a one-byte mask.
            stmt.mode = if opcode::op_supports(&name, Mode::Acc) {
                Mode::Acc
            } else if opcode::op_supports(&name, Mode::Imp8) {
                Mode::Imp8
            } else {
                Mode::Imp
            };
            return Ok(());
        }
        if opcode::op_supports(&name, Mode::Blk) && name != "BLK" {
            // MVN and MVP take two banks: `MVN dstbank,srcbank`, written as one
            // operand because the instruction's two bytes are the two banks.
            let mut comma = None;
            let mut depth = 0i32;
            for (index, token) in tokens.iter().enumerate().skip(pos) {
                let Token::Punct(text) = token else {
                    continue;
                };
                match text.as_str() {
                    "(" | "[" => depth += 1,
                    ")" | "]" => depth -= 1,
                    "," if depth == 0 => {
                        comma = Some(index);
                        break;
                    }
                    _ => {}
                }
            }
            let Some(comma) = comma else {
                return Err(Error::new(
                    file,
                    line,
                    format!("{name} needs two bank operands"),
                ));
            };
            let destination = parse_expr_range(tokens, pos, comma, file, line)?;
            let source = parse_expr_range(tokens, comma + 1, tokens.len(), file, line)?;
            stmt.operand = Some(Expr::Binary(
                BinOp::Add,
                Box::new(Expr::Binary(
                    BinOp::Mul,
                    Box::new(destination),
                    Box::new(Expr::Number(256)),
                )),
                Box::new(source),
            ));
            stmt.mode = Mode::Blk;
            return Ok(());
        }
        if tokens[pos].is_ident("A")
            && pos + 1 == tokens.len()
            && opcode::op_supports(&name, Mode::Acc)
        {
            stmt.mode = Mode::Acc;
            return Ok(());
        }
        if tokens[pos].is_punct("#") {
            stmt.operand = Some(ExprParser::new(tokens, pos + 1, file, line).parse()?);
            if opcode::op_supports(&name, Mode::Imm) {
                stmt.mode = Mode::Imm;
                if !stmt.imm_explicit && is_index_immediate(&name) {
                    // X and Y are sixteen bits wide on this machine's own
                    // conventions, and every widening instruction says `.B`.
                    stmt.imm_bytes = 2;
                }
            } else if opcode::op_supports(&name, Mode::Imm16) {
                stmt.mode = Mode::Imm16;
            } else if opcode::op_supports(&name, Mode::Imm8) {
                stmt.mode = Mode::Imm8;
            } else {
                // REP, SEP, BRK, COP, WDM.
                stmt.mode = Mode::Imp8;
            }
            return Ok(());
        }
        if tokens[pos].is_punct("[") {
            let after = match_close(tokens, pos, "[", "]", file, line)?;
            let indexed_y = looks_like_index(tokens, after, "Y");
            stmt.operand = Some(parse_expr_range(tokens, pos + 1, after - 1, file, line)?);
            stmt.mode = if indexed_y {
                Mode::DpLongY
            } else {
                pick_mode(&name, Mode::DpLong, Mode::AbsIndLong)
            };
            return Ok(());
        }
        if tokens[pos].is_punct("(") {
            let after = match_close(tokens, pos, "(", ")", file, line)?;
            let close = after - 1;
            let mut expr_end = close;
            let mut indexed_x = false;
            let mut stack_relative = false;
            if close >= pos + 4 && tokens[close - 2].is_punct(",") {
                if let Token::Ident(register) = &tokens[close - 1] {
                    if register == "X" {
                        indexed_x = true;
                        expr_end = close - 2;
                    } else if register == "S" {
                        stack_relative = true;
                        expr_end = close - 2;
                    }
                }
            }
            stmt.operand = Some(parse_expr_range(tokens, pos + 1, expr_end, file, line)?);
            if indexed_x {
                stmt.mode = pick_mode(&name, Mode::DpIndX, Mode::AbsIndX);
                if !opcode::op_supports(&name, stmt.mode)
                    && opcode::op_supports(&name, Mode::AbsIndLong)
                {
                    stmt.mode = Mode::AbsIndLong;
                }
                return Ok(());
            }
            if stack_relative {
                if !looks_like_index(tokens, after, "Y") {
                    return Err(Error::new(
                        file,
                        line,
                        "(expr,S) must be followed by ,Y on the 65816",
                    ));
                }
                stmt.mode = Mode::SrSY;
                return Ok(());
            }
            if looks_like_index(tokens, after, "Y") {
                // There is no absolute form of (dp),Y on the 65816.
                stmt.mode = Mode::DpIndY;
                return Ok(());
            }
            stmt.mode = pick_mode(&name, Mode::DpInd, Mode::AbsInd);
            return Ok(());
        }
        // Plain expression, optionally `,X`, `,Y` or `,S`.
        let mut expr_end = tokens.len();
        let mut indexed_x = false;
        let mut indexed_y = false;
        let mut stack_relative = false;
        let tail = tokens.len().saturating_sub(2);
        if looks_like_index(tokens, tail, "X") {
            indexed_x = true;
            expr_end = tail;
        } else if looks_like_index(tokens, tail, "Y") {
            indexed_y = true;
            expr_end = tail;
        } else if looks_like_index(tokens, tail, "S") {
            stack_relative = true;
            expr_end = tail;
        }
        stmt.operand = Some(parse_expr_range(tokens, pos, expr_end, file, line)?);
        if indexed_x {
            stmt.mode = if opcode::op_supports(&name, Mode::DpX) {
                Mode::DpX
            } else if opcode::op_supports(&name, Mode::AbsX) {
                Mode::AbsX
            } else {
                Mode::LongX
            };
            return Ok(());
        }
        if indexed_y {
            stmt.mode = if opcode::op_supports(&name, Mode::DpY) {
                Mode::DpY
            } else {
                Mode::AbsY
            };
            return Ok(());
        }
        if stack_relative {
            if !opcode::op_supports(&name, Mode::SrS) {
                return Err(Error::new(
                    file,
                    line,
                    format!("{name} does not support ,S"),
                ));
            }
            stmt.mode = Mode::SrS;
            return Ok(());
        }
        if opcode::op_supports(&name, Mode::Rel) {
            stmt.mode = Mode::Rel;
            return Ok(());
        }
        if opcode::op_supports(&name, Mode::Rel16) {
            stmt.mode = Mode::Rel16;
            return Ok(());
        }
        stmt.mode = if opcode::op_supports(&name, Mode::Dp) {
            Mode::Dp
        } else if opcode::op_supports(&name, Mode::Abs) {
            Mode::Abs
        } else if opcode::op_supports(&name, Mode::Long) {
            Mode::Long
        } else {
            // PEA is the only instruction whose bare operand is a sixteen-bit
            // immediate rather than an address.
            Mode::Imm16
        };
        Ok(())
    }

    fn parse_directive(
        &self,
        name: &str,
        stmt: &mut Statement,
        tokens: &[Token],
        pos: usize,
    ) -> Result<()> {
        let file = stmt.file.clone();
        let line = stmt.line;
        if name == ".ORG" {
            stmt.kind = StmtKind::Org;
            stmt.operand = Some(ExprParser::new(tokens, pos, &file, line).parse()?);
            return Ok(());
        }
        if name == ".BYTE" || name == ".DB" || name == ".WORD" || name == ".DW" {
            stmt.kind = if name == ".BYTE" || name == ".DB" {
                StmtKind::Byte
            } else {
                StmtKind::Word
            };
            let mut at = pos;
            while at < tokens.len() {
                let mut parser = ExprParser::new(tokens, at, &file, line);
                stmt.values.push(parser.parse()?);
                at = parser.position();
                if at < tokens.len() && tokens[at].is_punct(",") {
                    at += 1;
                    continue;
                }
                break;
            }
            return Ok(());
        }
        if name == ".FILL" {
            stmt.kind = StmtKind::Fill;
            let mut count_parser = ExprParser::new(tokens, pos, &file, line);
            stmt.operand = Some(count_parser.parse()?);
            let at = count_parser.position();
            if at >= tokens.len() || !tokens[at].is_punct(",") {
                return Err(Error::new(&file, line, ".fill needs a count and a byte"));
            }
            stmt.fill_value = Some(ExprParser::new(tokens, at + 1, &file, line).parse()?);
            return Ok(());
        }
        if name == ".RES" {
            stmt.kind = StmtKind::Reserve;
            stmt.operand = Some(ExprParser::new(tokens, pos, &file, line).parse()?);
            return Ok(());
        }
        if name == ".ALIGN" {
            stmt.kind = StmtKind::Align;
            let mut parser = ExprParser::new(tokens, pos, &file, line);
            stmt.operand = Some(parser.parse()?);
            let at = parser.position();
            if at < tokens.len() && tokens[at].is_punct(",") {
                stmt.fill_value = Some(ExprParser::new(tokens, at + 1, &file, line).parse()?);
            }
            return Ok(());
        }
        if name == ".TEXT" || name == ".ASCIIZ" {
            stmt.kind = StmtKind::Text;
            let Some(Token::Text(bytes)) = tokens.get(pos) else {
                return Err(Error::new(
                    &file,
                    line,
                    format!("{name} needs a quoted string"),
                ));
            };
            stmt.text = bytes.clone();
            if name == ".ASCIIZ" {
                stmt.text.push(0);
            }
            return Ok(());
        }
        if name == ".INCLUDE" {
            stmt.kind = StmtKind::Include;
            let Some(Token::Text(bytes)) = tokens.get(pos) else {
                return Err(Error::new(&file, line, ".include needs a quoted path"));
            };
            stmt.text = bytes.clone();
            return Ok(());
        }
        Err(Error::new(
            &file,
            line,
            format!("unknown directive: {name}"),
        ))
    }
}

/// The mode a mnemonic can actually encode, preferring the short form because
/// the emitter widens it when the value does not fit.
fn pick_mode(name: &str, narrow: Mode, wide: Mode) -> Mode {
    if opcode::op_supports(name, narrow) {
        narrow
    } else {
        wide
    }
}

/// The index just past the bracket that closes the one at `pos`.
fn match_close(
    tokens: &[Token],
    pos: usize,
    open: &str,
    close: &str,
    file: &str,
    line: usize,
) -> Result<usize> {
    let mut depth = 0i32;
    for (index, token) in tokens.iter().enumerate().skip(pos) {
        let Token::Punct(text) = token else {
            continue;
        };
        if text == open {
            depth += 1;
        } else if text == close {
            depth -= 1;
            if depth == 0 {
                return Ok(index + 1);
            }
        }
    }
    Err(Error::new(file, line, format!("unbalanced {open}{close}")))
}

/// An operand expression, with `$bb:aaaa` shorthand for a bank-qualified
/// address. The bank and the offset are combined as bank * 65536 + offset, so
/// the value is 24-bit and the emitter picks a long addressing mode.
fn parse_expr_range(
    tokens: &[Token],
    begin: usize,
    end: usize,
    file: &str,
    line: usize,
) -> Result<Expr> {
    if end < begin {
        return Err(Error::new(file, line, "expected an address"));
    }
    let mut depth = 0i32;
    for index in begin..end {
        let Token::Punct(text) = &tokens[index] else {
            continue;
        };
        match text.as_str() {
            "(" | "[" => depth += 1,
            ")" | "]" => depth -= 1,
            ":" if depth == 0 => {
                let bank = parse_plain(tokens, begin, index, file, line)?;
                let offset = parse_plain(tokens, index + 1, end, file, line)?;
                return Ok(Expr::Binary(
                    BinOp::Add,
                    Box::new(Expr::Binary(
                        BinOp::Mul,
                        Box::new(bank),
                        Box::new(Expr::Number(0x10000)),
                    )),
                    Box::new(offset),
                ));
            }
            _ => {}
        }
    }
    parse_plain(tokens, begin, end, file, line)
}

fn parse_plain(
    tokens: &[Token],
    begin: usize,
    end: usize,
    file: &str,
    line: usize,
) -> Result<Expr> {
    ExprParser::new(&tokens[begin..end], 0, file, line).parse()
}

/// Expands every `.include` in place, so the layout passes see one flat
/// program.
pub fn expand_includes(statements: &mut Vec<Statement>, depth: i32) -> Result<()> {
    let mut out = Vec::with_capacity(statements.len());
    for stmt in statements.drain(..) {
        if stmt.kind != StmtKind::Include {
            out.push(stmt);
            continue;
        }
        let path = String::from_utf8_lossy(&stmt.text).into_owned();
        let mut included = SourceParser::new(depth + 1).parse_file(&path)?;
        expand_includes(&mut included, depth + 1)?;
        out.extend(included);
    }
    *statements = out;
    Ok(())
}
