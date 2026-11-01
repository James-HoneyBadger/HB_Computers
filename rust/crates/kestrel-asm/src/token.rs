// SPDX-License-Identifier: MIT
//
// The lexer: one source line in, the tokens the parser and the expression
// grammar work on.
//
// Identifiers are folded to upper case as they are read, which is what makes
// labels case-blind. A number is read where it stands -- `$FF`, `%1010`, `0x34`
// or plain decimal -- and a number that runs into a letter is refused rather
// than silently becoming a shorter number.
//
// The line is handled as bytes, not as text: a source file is whatever the
// editor wrote, and only the ASCII part of it means anything here.

use crate::error::{Error, Result};

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Token {
    Ident(String),
    Number(i64),
    /// The contents of a quoted string, kept as bytes so `.text` emits exactly
    /// what was written.
    Text(Vec<u8>),
    Punct(String),
}

impl Token {
    pub fn is_punct(&self, text: &str) -> bool {
        matches!(self, Token::Punct(found) if found == text)
    }

    pub fn is_ident(&self, text: &str) -> bool {
        matches!(self, Token::Ident(found) if found == text)
    }

    pub fn spelling_or_blank(&self) -> String {
        match self {
            Token::Ident(text) | Token::Punct(text) => text.clone(),
            Token::Number(value) => value.to_string(),
            Token::Text(bytes) => String::from_utf8_lossy(bytes).into_owned(),
        }
    }
}

fn ident_start(ch: u8) -> bool {
    ch.is_ascii_alphabetic() || ch == b'_'
}

fn ident_char(ch: u8) -> bool {
    ch.is_ascii_alphanumeric() || ch == b'_' || ch == b'.'
}

fn is_space(ch: u8) -> bool {
    matches!(ch, b' ' | b'\t' | b'\n' | 0x0B | 0x0C | b'\r')
}

fn hex_digit(ch: u8) -> Option<i64> {
    match ch {
        b'0'..=b'9' => Some(i64::from(ch - b'0')),
        b'a'..=b'f' => Some(i64::from(ch - b'a') + 10),
        b'A'..=b'F' => Some(i64::from(ch - b'A') + 10),
        _ => None,
    }
}

/// The characters that stand alone as one punctuation token.
const PUNCTUATION: &str = "#(),+-*/&|^<>=:.@";

pub fn lex(line: &[u8], file: &str, number: usize) -> Result<Vec<Token>> {
    let mut tokens = Vec::new();
    let mut i = 0;
    while i < line.len() {
        let ch = line[i];
        if is_space(ch) {
            i += 1;
            continue;
        }
        if ch == b';' {
            break;
        }
        // A dot only starts a token when a name follows it, so `.org` is one
        // identifier while a lone `.` stays the current-address operator.
        if ident_start(ch) || (ch == b'.' && i + 1 < line.len() && ident_start(line[i + 1])) {
            let start = i;
            i += 1;
            while i < line.len() && ident_char(line[i]) {
                i += 1;
            }
            let text = String::from_utf8_lossy(&line[start..i]).to_ascii_uppercase();
            tokens.push(Token::Ident(text));
            continue;
        }
        if ch == b'$' || ch == b'%' || ch.is_ascii_digit() {
            let start = i;
            let mut value = 0i64;
            if ch == b'$' {
                i += 1;
                let digits = i;
                while i < line.len() {
                    let Some(digit) = hex_digit(line[i]) else {
                        break;
                    };
                    value = value.wrapping_mul(16).wrapping_add(digit);
                    i += 1;
                }
                if i == digits {
                    return Err(Error::new(file, number, "empty hexadecimal number"));
                }
            } else if ch == b'%' {
                i += 1;
                let digits = i;
                while i < line.len() && (line[i] == b'0' || line[i] == b'1') {
                    value = value
                        .wrapping_mul(2)
                        .wrapping_add(i64::from(line[i] - b'0'));
                    i += 1;
                }
                if i == digits {
                    return Err(Error::new(file, number, "empty binary number"));
                }
            } else if ch == b'0'
                && i + 1 < line.len()
                && (line[i + 1] == b'x' || line[i + 1] == b'X')
            {
                // 0x: the other way of writing hex.
                i += 2;
                let digits = i;
                while i < line.len() {
                    let Some(digit) = hex_digit(line[i]) else {
                        break;
                    };
                    value = value.wrapping_mul(16).wrapping_add(digit);
                    i += 1;
                }
                if i == digits {
                    return Err(Error::new(file, number, "empty hexadecimal number"));
                }
            } else {
                while i < line.len() && line[i].is_ascii_digit() {
                    value = value
                        .wrapping_mul(10)
                        .wrapping_add(i64::from(line[i] - b'0'));
                    i += 1;
                }
                if i < line.len() && ident_char(line[i]) {
                    let run = String::from_utf8_lossy(&line[start..=i]);
                    return Err(Error::new(
                        file,
                        number,
                        format!("a number cannot run into a letter: {run}"),
                    ));
                }
            }
            tokens.push(Token::Number(value));
            continue;
        }
        if ch == b'\'' {
            if i + 2 >= line.len() || line[i + 2] != b'\'' {
                return Err(Error::new(file, number, "unterminated character literal"));
            }
            tokens.push(Token::Number(i64::from(line[i + 1])));
            i += 3;
            continue;
        }
        if ch == b'"' {
            i += 1;
            let start = i;
            while i < line.len() && line[i] != b'"' {
                i += 1;
            }
            if i >= line.len() {
                return Err(Error::new(file, number, "unterminated string literal"));
            }
            tokens.push(Token::Text(line[start..i].to_vec()));
            i += 1;
            continue;
        }
        if ch == b'<' && i + 1 < line.len() && line[i + 1] == b'<' {
            tokens.push(Token::Punct("<<".to_string()));
            i += 2;
            continue;
        }
        if ch == b'>' && i + 1 < line.len() && line[i + 1] == b'>' {
            tokens.push(Token::Punct(">>".to_string()));
            i += 2;
            continue;
        }
        if PUNCTUATION.as_bytes().contains(&ch) {
            tokens.push(Token::Punct(char::from(ch).to_string()));
            i += 1;
            continue;
        }
        return Err(Error::new(
            file,
            number,
            format!("unexpected character '{}'", char::from(ch)),
        ));
    }
    Ok(tokens)
}
