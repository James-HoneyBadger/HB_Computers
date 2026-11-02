// SPDX-License-Identifier: MIT
//
// The expression grammar: the usual C-ish set over numbers, symbols, `.` (the
// current address) and the low and high byte operators `<` and `>`.
//
// Parsing builds a tree rather than folding on the spot, because the same
// expression is evaluated once per layout pass and only the final pass has
// every symbol it needs. Evaluation therefore takes the symbol table with it,
// and can be told to treat an unknown symbol as zero: that is what the early
// passes do for a forward reference.

use std::collections::BTreeMap;

use crate::error::{Error, Result};
use crate::token::Token;

/// Every symbol the assembler has defined so far, by upper-case name.
pub type SymbolTable = BTreeMap<String, i64>;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum BinOp {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    And,
    Or,
    Xor,
    Shl,
    Shr,
}

#[derive(Clone, PartialEq, Eq, Debug)]
pub enum Expr {
    Number(i64),
    Symbol(String),
    Here,
    Negate(Box<Expr>),
    Not(Box<Expr>),
    Lo(Box<Expr>),
    Hi(Box<Expr>),
    Binary(BinOp, Box<Expr>, Box<Expr>),
}

struct BinaryOpDef {
    text: &'static str,
    op: BinOp,
    precedence: i32,
}

/// Binary operators from loosest to tightest binding.
#[rustfmt::skip]
const BINARY_OPS: &[BinaryOpDef] = &[
    BinaryOpDef { text: "|", op: BinOp::Or, precedence: 1 },
    BinaryOpDef { text: "^", op: BinOp::Xor, precedence: 2 },
    BinaryOpDef { text: "&", op: BinOp::And, precedence: 3 },
    BinaryOpDef { text: "<<", op: BinOp::Shl, precedence: 4 },
    BinaryOpDef { text: ">>", op: BinOp::Shr, precedence: 4 },
    BinaryOpDef { text: "+", op: BinOp::Add, precedence: 5 },
    BinaryOpDef { text: "-", op: BinOp::Sub, precedence: 5 },
    BinaryOpDef { text: "*", op: BinOp::Mul, precedence: 6 },
    BinaryOpDef { text: "/", op: BinOp::Div, precedence: 6 },
    BinaryOpDef { text: "%", op: BinOp::Mod, precedence: 6 },
];

pub struct ExprParser<'a> {
    tokens: &'a [Token],
    pos: usize,
    file: &'a str,
    line: usize,
}

impl<'a> ExprParser<'a> {
    pub fn new(tokens: &'a [Token], begin: usize, file: &'a str, line: usize) -> Self {
        Self {
            tokens,
            pos: begin,
            file,
            line,
        }
    }

    pub fn parse(&mut self) -> Result<Expr> {
        self.parse_binary(0)
    }

    /// Where parsing stopped, so a caller with a list of expressions knows
    /// which token comes next.
    pub fn position(&self) -> usize {
        self.pos
    }

    /// A string ends the expression: it is never part of one.
    fn at_end(&self) -> bool {
        match self.tokens.get(self.pos) {
            None => true,
            Some(Token::Text(_)) => true,
            Some(_) => false,
        }
    }

    fn current(&self) -> Option<&Token> {
        if self.at_end() {
            None
        } else {
            self.tokens.get(self.pos)
        }
    }

    fn is_punct(&self, text: &str) -> bool {
        self.current().is_some_and(|token| token.is_punct(text))
    }

    fn parse_binary(&mut self, min_precedence: i32) -> Result<Expr> {
        let mut left = self.parse_unary()?;
        while let Some(token) = self.current() {
            let Token::Punct(text) = token else {
                break;
            };
            let Some(found) = BINARY_OPS.iter().find(|op| op.text == text.as_str()) else {
                break;
            };
            if found.precedence < min_precedence {
                break;
            }
            let op = found.op;
            let precedence = found.precedence;
            self.pos += 1;
            let right = self.parse_binary(precedence + 1)?;
            left = Expr::Binary(op, Box::new(left), Box::new(right));
        }
        Ok(left)
    }

    fn parse_unary(&mut self) -> Result<Expr> {
        if self.at_end() {
            return Err(Error::new(self.file, self.line, "expected an expression"));
        }
        let token = self.tokens[self.pos].clone();
        if let Token::Punct(text) = &token {
            match text.as_str() {
                "-" => {
                    self.pos += 1;
                    return Ok(Expr::Negate(Box::new(self.parse_unary()?)));
                }
                "<" => {
                    self.pos += 1;
                    return Ok(Expr::Lo(Box::new(self.parse_unary()?)));
                }
                ">" => {
                    self.pos += 1;
                    return Ok(Expr::Hi(Box::new(self.parse_unary()?)));
                }
                // The lexer does not hand out `~` today -- it is not in the
                // single-character set -- but the grammar has the operator, and
                // the original's parser keeps the two in step.
                "~" => {
                    self.pos += 1;
                    return Ok(Expr::Not(Box::new(self.parse_unary()?)));
                }
                "(" => {
                    self.pos += 1;
                    let inner = self.parse_binary(0)?;
                    if !self.is_punct(")") {
                        return Err(Error::new(self.file, self.line, "expected ')'"));
                    }
                    self.pos += 1;
                    return Ok(inner);
                }
                "." => {
                    self.pos += 1;
                    return Ok(Expr::Here);
                }
                _ => {}
            }
        }
        match token {
            Token::Number(value) => {
                self.pos += 1;
                Ok(Expr::Number(value))
            }
            Token::Ident(name) => {
                self.pos += 1;
                Ok(Expr::Symbol(name))
            }
            _ => Err(Error::new(
                self.file,
                self.line,
                format!(
                    "unexpected token in expression: {}",
                    token.spelling_or_blank()
                ),
            )),
        }
    }
}

/// A symbol lookup, so the caller decides where the values live: the layout
/// passes answer from two tables at once, and a test from a plain one.
pub type Lookup<'a> = &'a dyn Fn(&str) -> Option<i64>;

pub fn evaluate(
    node: &Expr,
    lookup: Lookup<'_>,
    here: i64,
    file: &str,
    line: usize,
    allow_unknown: bool,
) -> Result<i64> {
    let evaluate = |node: &Expr| evaluate(node, lookup, here, file, line, allow_unknown);
    let binary = |left: &Expr, right: &Expr, op: BinOp| -> Result<i64> {
        let left = evaluate(left)?;
        let right = evaluate(right)?;
        Ok(match op {
            BinOp::Add => left.wrapping_add(right),
            BinOp::Sub => left.wrapping_sub(right),
            BinOp::Mul => left.wrapping_mul(right),
            BinOp::Div => {
                if right == 0 {
                    return Err(Error::new(file, line, "division by zero in expression"));
                }
                if left == i64::MIN && right == -1 {
                    i64::MIN
                } else {
                    left / right
                }
            }
            BinOp::Mod => {
                if right == 0 {
                    return Err(Error::new(file, line, "division by zero in expression"));
                }
                if left == i64::MIN && right == -1 {
                    0
                } else {
                    left % right
                }
            }
            BinOp::And => left & right,
            BinOp::Or => left | right,
            BinOp::Xor => left ^ right,
            BinOp::Shl => left.wrapping_shl(right as u32),
            BinOp::Shr => left.wrapping_shr(right as u32),
        })
    };
    match node {
        Expr::Number(value) => Ok(*value),
        Expr::Here => Ok(here),
        Expr::Symbol(name) => match lookup(name) {
            Some(value) => Ok(value),
            None => {
                if allow_unknown {
                    // A forward reference in an early layout pass: the size it
                    // will eventually produce is not known yet, and pretending
                    // it is a byte is how a two-pass assembler gets the wrong
                    // zero-page form. The fixed-point loop settles it.
                    Ok(0)
                } else {
                    Err(Error::new(file, line, format!("unknown symbol: {name}")))
                }
            }
        },
        Expr::Negate(inner) => Ok(evaluate(inner)?.wrapping_neg()),
        Expr::Not(inner) => Ok(!evaluate(inner)?),
        Expr::Lo(inner) => Ok(evaluate(inner)? & 0xFF),
        Expr::Hi(inner) => Ok((evaluate(inner)? >> 8) & 0xFF),
        Expr::Binary(op, left, right) => binary(left, right, *op),
    }
}
