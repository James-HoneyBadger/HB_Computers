// SPDX-License-Identifier: MIT
//
// kestrel16-asm -- the port of `tools/kestrel16_asm.cpp`.
//
// This is the assembler that turns `roms/kestrel16/*.s` into the images the
// Kestrel 16 boots. The port follows the original part by part, so the two can
// be read side by side: `token.rs` is the lexer, `opcode.rs` the instruction
// set, `expr.rs` the expression grammar, `source.rs` the statement parser,
// `assembler.rs` the layout passes, and `output.rs` the files the build asks
// for. `main.rs` is the command line, unchanged in spelling and in exit codes.
//
// It is a standalone tool rather than a layer over `kestrel-asm`, for the same
// reason the C++ tree has two tools: the instruction set, the addressing modes,
// the width rules and the address space are different, and the only parts the
// two share -- the lexer, the expression grammar and the shape of the layout
// loop -- are small enough to be read twice. Sharing them would have meant
// teaching `kestrel-asm` about `[`, `.B` and 24-bit addresses, which is to say
// changing the 6502 tool to assemble a program it never sees.
//
// Layout is resolved by iterating to a fixed point rather than by the usual
// single two-pass guess, so a label that lands in the direct page needs no hint
// and cannot oscillate: the images are byte-for-byte reproducible.

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
