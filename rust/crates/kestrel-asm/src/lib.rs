// SPDX-License-Identifier: MIT
//
// kestrel-asm -- the port of `tools/kestrel_asm.cpp`.
//
// This is the assembler that turns `roms/kestrel/*.s` into the images the
// machine boots. The port follows the original part by part, so the two can be
// read side by side: `token.rs` is the lexer, `opcode.rs` the instruction set,
// `expr.rs` the expression grammar, `source.rs` the statement parser,
// `assembler.rs` the layout passes, and `output.rs` the files the build asks
// for. `main.rs` is the command line, unchanged in spelling and in exit codes.
//
// Layout is resolved by iterating to a fixed point rather than by the usual
// single two-pass guess, so a label that lands in zero page needs no hint and
// cannot oscillate: the images are byte-for-byte reproducible.

pub mod assembler;
pub mod error;
pub mod expr;
pub mod opcode;
pub mod output;
pub mod source;
pub mod token;

pub use assembler::{Assembler, Assembly, ListingEntry, Options};
pub use error::{Error, Result};
pub use opcode::Mode;
pub use source::{expand_includes, SourceParser, Statement};
