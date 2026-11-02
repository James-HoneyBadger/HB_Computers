// SPDX-License-Identifier: MIT
//
// kestrel16_asm -- the command line front of the assembler.
//
// The spelling of every option, and the exit codes, are the C++ tool's: 0 for
// success, 1 for a diagnostic on stderr, 2 for being given no source at all.
// Anything that goes wrong inside assembly is reported the same way, as
// `kestrel_asm: file:line: what was wrong` -- the prefix included, because the
// C++ tool was copied from the 6502 one and never renamed its messages, and a
// build script that reads a diagnostic should not have to know which of the two
// tools wrote it.
//
//   kestrel16_asm <input.s> [--name NAME] [--origin HEX] [--size N] [--fill HEX]
//                 [--header FILE] [--bin FILE] [--symbols FILE] [--listing FILE]

use std::process::ExitCode;

use kestrel16_asm::assembler::Options;
use kestrel16_asm::output::{header_text, listing_text, symbol_text};
use kestrel16_asm::{expand_includes, Assembler, SourceParser};

/// The C++ tool's help text, which names the 6502 binary in its usage line for
/// the same reason its diagnostics do: the two tools were written as one and
/// this output has been that way since.
const USAGE: &str = "\
usage: kestrel_asm <source.s> [--name basic] [--origin $A000]
              [--size 16384] [--fill $FF]
              [--header out.hpp] [--bin out.bin]
              [--symbols out.sym] [--listing out.lst]
";

enum Outcome {
    Assembled,
    Help,
}

enum Failure {
    NoSource,
    Message(String),
}

struct Command {
    source: String,
    name: Option<String>,
    options: Options,
    header: Option<String>,
    binary: Option<String>,
    symbols: Option<String>,
    listing: Option<String>,
}

fn main() -> ExitCode {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    match run(&arguments) {
        Ok(Outcome::Help) => {
            print!("{USAGE}");
            ExitCode::SUCCESS
        }
        Ok(Outcome::Assembled) => ExitCode::SUCCESS,
        Err(Failure::NoSource) => {
            eprintln!("kestrel_asm: no source file");
            ExitCode::from(2)
        }
        Err(Failure::Message(message)) => {
            eprintln!("kestrel_asm: {message}");
            ExitCode::FAILURE
        }
    }
}

fn run(arguments: &[String]) -> Result<Outcome, Failure> {
    let mut command = Command {
        source: String::new(),
        name: None,
        options: Options::default(),
        header: None,
        binary: None,
        symbols: None,
        listing: None,
    };
    let mut index = 0;
    while index < arguments.len() {
        let argument = arguments[index].as_str();
        match argument {
            "--origin" => command.options.origin = parse_number(&value_of(arguments, &mut index)?)?,
            "--size" => command.options.size = parse_number(&value_of(arguments, &mut index)?)?,
            "--fill" => {
                command.options.fill = parse_number(&value_of(arguments, &mut index)?)? as u8
            }
            "--header" => command.header = Some(value_of(arguments, &mut index)?),
            "--symbols" => command.symbols = Some(value_of(arguments, &mut index)?),
            "--bin" => command.binary = Some(value_of(arguments, &mut index)?),
            "--listing" => command.listing = Some(value_of(arguments, &mut index)?),
            "--name" => command.name = Some(value_of(arguments, &mut index)?),
            "--help" | "-h" => return Ok(Outcome::Help),
            _ if argument.starts_with('-') => {
                return Err(Failure::Message(format!("unknown option {argument}")));
            }
            _ => command.source = argument.to_string(),
        }
        index += 1;
    }
    if command.source.is_empty() {
        return Err(Failure::NoSource);
    }
    assemble(&command)?;
    Ok(Outcome::Assembled)
}

fn value_of(arguments: &[String], index: &mut usize) -> Result<String, Failure> {
    let argument = arguments[*index].clone();
    *index += 1;
    arguments
        .get(*index)
        .cloned()
        .ok_or_else(|| Failure::Message(format!("missing value for {argument}")))
}

fn assemble(command: &Command) -> Result<(), Failure> {
    let entry_name = match &command.name {
        Some(name) => name.clone(),
        None => default_name(&command.source),
    };
    let mut statements = SourceParser::new(0)
        .parse_file(&command.source)
        .map_err(describe)?;
    expand_includes(&mut statements, 0).map_err(describe)?;
    let assembly = Assembler::new(statements, command.options, entry_name.clone())
        .assemble()
        .map_err(describe)?;

    if let Some(path) = &command.binary {
        write(path, &assembly.image)?;
    }
    if let Some(path) = &command.header {
        write(
            path,
            header_text(&entry_name, command.options.origin, &assembly.image).as_bytes(),
        )?;
    }
    if let Some(path) = &command.symbols {
        write(path, symbol_text(&assembly.symbols).as_bytes())?;
    }
    if let Some(path) = &command.listing {
        write(
            path,
            &listing_text(&assembly.listing, &assembly.image, command.options.origin),
        )?;
    }
    Ok(())
}

fn describe(error: kestrel16_asm::Error) -> Failure {
    Failure::Message(error.to_string())
}

/// The file stem of the source, which is the name that ends up in the generated
/// header.
fn default_name(source: &str) -> String {
    let base = match source.rsplit_once('/') {
        Some((_, base)) => base,
        None => source,
    };
    match base.split_once('.') {
        Some((stem, _)) => stem.to_string(),
        None => base.to_string(),
    }
}

/// A number as the command line writes one: `$A000` in hex, or the usual C
/// spellings (`0x` for hex, a leading zero for octal) in decimal otherwise.
/// Trailing characters are ignored, as the C library's own conversion does, so
/// `--origin 0x` is zero rather than an error.
fn parse_number(text: &str) -> Result<u32, Failure> {
    let text = text.trim_start();
    let (negative, unsigned) = match text.strip_prefix('-') {
        Some(rest) => (true, rest),
        None => (false, text.strip_prefix('+').unwrap_or(text)),
    };
    let mut radix = 10u32;
    let mut digits = unsigned;
    if let Some(rest) = unsigned.strip_prefix('$') {
        radix = 16;
        digits = rest;
    } else if let Some(rest) = unsigned
        .strip_prefix("0x")
        .or_else(|| unsigned.strip_prefix("0X"))
    {
        if rest.starts_with(|ch: char| ch.is_ascii_hexdigit()) {
            radix = 16;
            digits = rest;
        } else {
            // `0x` with nothing after it is just a zero in octal.
            radix = 8;
        }
    } else if unsigned.len() > 1 && unsigned.starts_with('0') {
        radix = 8;
    }
    let mut value: u64 = 0;
    let mut taken = 0;
    for ch in digits.chars() {
        let Some(digit) = ch.to_digit(radix) else {
            break;
        };
        value = value
            .checked_mul(u64::from(radix))
            .and_then(|value| value.checked_add(u64::from(digit)))
            .ok_or_else(|| Failure::Message(format!("number is too large: {text}")))?;
        taken += 1;
    }
    if taken == 0 {
        return Err(Failure::Message(format!("not a number: {text}")));
    }
    let value = if negative {
        0u64.wrapping_sub(value)
    } else {
        value
    };
    Ok(value as u32)
}

fn write(path: &str, bytes: &[u8]) -> Result<(), Failure> {
    std::fs::write(path, bytes).map_err(|_| Failure::Message(format!("cannot write {path}")))
}
