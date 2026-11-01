// SPDX-License-Identifier: MIT
//
// The 6502 assembler: the dialect the Kestrel's ROMs are written in.
//
// Every encoding here is written out by hand from the processor's own opcode
// map rather than recorded from the tool, so a bug in the table cannot pass by
// agreeing with itself. The tool is run as a program, which is how the build
// uses it: `CARGO_BIN_EXE_kestrel_asm` is what the C++ tests reach for through
// the `KESTREL_ASM_PATH` compile definition.

use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicUsize, Ordering};

const ORIGIN: u32 = 0x8000;
const SIZE: u32 = 0x1000;

/// A sentinel after the code makes every case length-exact: the image is padded
/// to its full size, so the tail would otherwise go unchecked.
const SENTINEL: u8 = 0xEE;

const TOOL: &str = env!("CARGO_BIN_EXE_kestrel_asm");

/// A name no other case can take, so the cases can be assembled side by side.
fn unique(stem: &str) -> PathBuf {
    static NEXT: AtomicUsize = AtomicUsize::new(0);
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join("kestrel_asm_tests");
    std::fs::create_dir_all(&dir).expect("a directory to write cases in");
    dir.join(format!("{stem}-{}", NEXT.fetch_add(1, Ordering::Relaxed)))
}

fn command(source: &Path, binary: &Path) -> Command {
    let mut command = Command::new(TOOL);
    command
        .arg(source)
        .arg("--origin")
        .arg(ORIGIN.to_string())
        .arg("--size")
        .arg(SIZE.to_string())
        .arg("--bin")
        .arg(binary)
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    command
}

fn assemble(source: &str) -> Option<Vec<u8>> {
    let stem = unique("case");
    let src = stem.with_extension("s");
    let bin = stem.with_extension("bin");
    std::fs::write(
        &src,
        format!("  .org ${ORIGIN:X}\n{source}\n  .byte ${SENTINEL:X}\n"),
    )
    .expect("a case to write");
    let _ = std::fs::remove_file(&bin);
    if !command(&src, &bin)
        .status()
        .expect("the tool to run")
        .success()
    {
        println!("     assembly failed for:\n{source}");
        return None;
    }
    std::fs::read(&bin).ok()
}

fn bytes_of(bytes: &[u8]) -> String {
    let mut text = String::new();
    for byte in bytes {
        text.push_str(&format!("{byte:02X} "));
    }
    text.pop();
    text
}

fn expect(source: &str, expected: &[u8]) {
    let Some(got) = assemble(source) else {
        panic!("assembly failed for: {source}");
    };
    let mut wanted = expected.to_vec();
    wanted.push(SENTINEL);
    let seen = &got[..got.len().min(wanted.len())];
    assert_eq!(bytes_of(seen), bytes_of(&wanted), "     in: {source}");
}

/// A source the assembler has to refuse.
fn expect_refused(source: &str, because: &str) {
    let stem = unique("bad");
    let src = stem.with_extension("s");
    let bin = stem.with_extension("bin");
    std::fs::write(&src, format!("  .org ${ORIGIN:X}\n{source}\n")).expect("a case to write");
    let _ = std::fs::remove_file(&bin);
    let refused = !command(&src, &bin)
        .status()
        .expect("the tool to run")
        .success();
    assert!(refused, "     accepted: {source} ({because})");
}

#[test]
fn every_addressing_mode() {
    expect("  LDA #$12", &[0xA9, 0x12]);
    expect("  LDA $10", &[0xA5, 0x10]);
    expect("  LDA $10,X", &[0xB5, 0x10]);
    expect("  LDX $10,Y", &[0xB6, 0x10]);
    expect("  LDA $1234", &[0xAD, 0x34, 0x12]);
    expect("  LDA $1234,X", &[0xBD, 0x34, 0x12]);
    expect("  LDA $1234,Y", &[0xB9, 0x34, 0x12]);
    expect("  LDA ($10,X)", &[0xA1, 0x10]);
    expect("  LDA ($10),Y", &[0xB1, 0x10]);
    expect("  JMP ($1234)", &[0x6C, 0x34, 0x12]);
    expect("  JSR $1234", &[0x20, 0x34, 0x12]);
    expect("  RTS", &[0x60]);
    expect("  STA $8000", &[0x8D, 0x00, 0x80]);
    expect("  BRK", &[0x00]);
}

#[test]
fn numbers_and_expressions() {
    // The two ways of writing hex, and binary, end up the same byte.
    expect("  .byte $34, %110100, 52", &[0x34, 0x34, 0x34]);
    expect("  .byte 0x34", &[0x34]);
    // Character literals, and the operators the dialect allows.
    expect(
        "  .byte 'A', 'A'+1, <$1234, >$1234",
        &[0x41, 0x42, 0x34, 0x12],
    );
    expect(
        "  .byte 1+2*3, 7&3, 8|1, 1<<4, 20>>2",
        &[0x07, 0x03, 0x09, 0x10, 0x05],
    );
    expect("  .word $1234+1", &[0x35, 0x12]);
}

#[test]
fn data_directives() {
    expect(
        "  .byte $41, $42\n  .text \"HI\"\n  .asciiz \"YO\"",
        &[0x41, 0x42, 0x48, 0x49, 0x59, 0x4F, 0x00],
    );
    expect(
        "  .fill 3, $AA\n  .res 2\n  .byte $BB",
        &[0xAA, 0xAA, 0xAA, 0x00, 0x00, 0xBB],
    );
    expect(
        "  .byte 1\n  .align 8\n  .byte 2",
        &[0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02],
    );
    expect("  .dw $1234\n  .db $56", &[0x34, 0x12, 0x56]);
}

#[test]
fn labels_and_branches() {
    // A branch to its own line is four bytes from the opcode: the offset counts
    // from the instruction after the branch.
    expect("again:\n  LDA $10\n  BNE again", &[0xA5, 0x10, 0xD0, 0xFC]);
    // Labels are case-blind.
    expect("Here: NOP\n  JMP here", &[0xEA, 0x4C, 0x00, 0x80]);
    // A near branch that cannot reach becomes the inverse branch over a JMP.
    let mut far = vec![0xF0, 0x03, 0x4C, 0xCD, 0x80];
    far.extend(std::iter::repeat_n(0xEA, 200));
    far.push(0x60);
    expect("  BNE far\n  .fill 200, $EA\nfar:\n  RTS", &far);
    // A constant, and a label used before it is defined.
    expect(
        "SIZE = 3\n  .byte SIZE\n  .word base\nbase:",
        &[0x03, 0x03, 0x80],
    );
}

#[test]
fn refusals() {
    // A number that runs into a letter would otherwise become a silent zero.
    expect_refused("  .byte 12AB", "a number that runs into a letter");
    expect_refused("  .byte $", "empty hexadecimal number");
}
