// SPDX-License-Identifier: MIT
//
// The statement parser: source lines in, the flat list of statements the layout
// passes walk.
//
// The dialect it accepts is the one the Kestrel's ROMs are written in: a label
// at the current address (with or without a colon, in column 0 or not), `NAME =
// expr` for a constant, the handful of data directives, and the instructions.
//
// Includes are expanded here, in place, so the passes see one flat program: the
// included file's own `.org` and labels do the rest. A ROM is a single image, so
// an include is textual rather than a separate link.

use crate::error::{Error, Result};
use crate::expr::{Expr, ExprParser};
use crate::opcode::{self, Mode};
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

/// `,X` / `,Y` at `pos`.
fn looks_like_index(tokens: &[Token], pos: usize, register: &str) -> bool {
    pos + 1 < tokens.len() && tokens[pos].is_punct(",") && tokens[pos + 1].is_ident(register)
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
                if !is_directive(name) && !opcode::is_mnemonic(name) {
                    // The column-0 form: `LOOP  DEX`. An identifier in the label
                    // field is a label whether or not it carries the colon.
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
        if is_directive(head) {
            let mut stmt = Statement::new(StmtKind::Instruction, file, number, raw);
            stmt.label = label;
            self.parse_directive(head, &mut stmt, &tokens, pos + 1)?;
            out.push(stmt);
            return Ok(());
        }
        let mut stmt = Statement::new(StmtKind::Instruction, file, number, raw);
        stmt.label = label;
        stmt.mnemonic = head.clone();
        if !opcode::is_mnemonic(head) {
            return Err(Error::new(
                file,
                number,
                format!("unknown mnemonic: {head}"),
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
            // `ASL` with nothing after it shifts the accumulator: the 6502's
            // implied form does not exist for the shift mnemonics.
            stmt.mode = if opcode::op_supports(&name, Mode::Acc) {
                Mode::Acc
            } else {
                Mode::Imp
            };
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
            stmt.mode = Mode::Imm;
            return Ok(());
        }
        if tokens[pos].is_punct("(") {
            // `(expr,X)` or `(expr),Y` or `(expr)`.
            let mut close = pos + 1;
            let mut depth = 1;
            while close < tokens.len() && depth > 0 {
                if tokens[close].is_punct("(") {
                    depth += 1;
                } else if tokens[close].is_punct(")") {
                    depth -= 1;
                }
                close += 1;
            }
            if depth != 0 {
                return Err(Error::new(file, line, "unbalanced parentheses"));
            }
            let inner_end = close - 1;
            let mut expr_end = inner_end;
            let mut indexed_x = false;
            if inner_end >= 2
                && tokens[inner_end - 2].is_punct(",")
                && tokens[inner_end - 1].is_ident("X")
            {
                indexed_x = true;
                expr_end = inner_end - 2;
            }
            stmt.operand =
                Some(ExprParser::new(&tokens[pos + 1..expr_end], 0, file, line).parse()?);
            if indexed_x {
                stmt.mode = Mode::IndX;
                return Ok(());
            }
            if close < tokens.len() && tokens[close].is_punct(",") {
                if close + 1 < tokens.len() && tokens[close + 1].is_ident("Y") {
                    stmt.mode = Mode::IndY;
                    return Ok(());
                }
                return Err(Error::new(file, line, "expected Y after (indirect),"));
            }
            stmt.mode = Mode::Ind;
            return Ok(());
        }
        // Plain expression, optionally `,X` or `,Y`.
        let mut expr_end = tokens.len();
        let mut indexed_x = false;
        let mut indexed_y = false;
        if tokens.len() >= 2 && looks_like_index(tokens, tokens.len() - 2, "X") {
            indexed_x = true;
            expr_end = tokens.len() - 2;
        } else if tokens.len() >= 2 && looks_like_index(tokens, tokens.len() - 2, "Y") {
            indexed_y = true;
            expr_end = tokens.len() - 2;
        }
        stmt.operand = Some(ExprParser::new(&tokens[pos..expr_end], 0, file, line).parse()?);
        if indexed_x {
            // Resolved to absolute,X later if the value turns out to be large.
            stmt.mode = if opcode::op_supports(&name, Mode::ZpX) {
                Mode::ZpX
            } else {
                Mode::AbsX
            };
            return Ok(());
        }
        if indexed_y {
            stmt.mode = if opcode::op_supports(&name, Mode::ZpY) {
                Mode::ZpY
            } else {
                Mode::AbsY
            };
            return Ok(());
        }
        if opcode::is_branch(&name) {
            stmt.mode = Mode::Rel;
            return Ok(());
        }
        stmt.mode = if opcode::op_supports(&name, Mode::Zp) {
            Mode::Zp
        } else {
            Mode::Abs
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

/// Expands every `.include` in place, so the layout passes see one flat
/// program.
/// An include is looked for beside the file that asked for it before it is looked
/// for in the working directory, so a tree of sources assembles from wherever it
/// is called -- which the C++ tool only manages because its build sets the
/// working directory to the ROM's own.
fn resolve_include(from: &str, wanted: &str) -> String {
    if std::path::Path::new(wanted).is_absolute() {
        return wanted.to_string();
    }
    let beside = std::path::Path::new(from)
        .parent()
        .map(|directory| directory.join(wanted));
    match beside {
        Some(candidate) if candidate.is_file() => candidate.to_string_lossy().into_owned(),
        _ => wanted.to_string(),
    }
}

pub fn expand_includes(statements: &mut Vec<Statement>, depth: i32) -> Result<()> {
    let mut out = Vec::with_capacity(statements.len());
    for stmt in statements.drain(..) {
        if stmt.kind != StmtKind::Include {
            out.push(stmt);
            continue;
        }
        let path = resolve_include(&stmt.file, &String::from_utf8_lossy(&stmt.text));
        let mut included = SourceParser::new(depth + 1).parse_file(&path)?;
        expand_includes(&mut included, depth + 1)?;
        out.extend(included);
    }
    *statements = out;
    Ok(())
}
