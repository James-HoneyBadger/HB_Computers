// SPDX-License-Identifier: MIT
//
// The 65816 assembler: the dialect the Kestrel 16's ROMs are written in.
//
// Every encoding here is written out by hand from the processor's own opcode
// map rather than recorded from the tool, so a bug in the table cannot pass by
// agreeing with itself. The tool is run as a program, which is how the build
// uses it: `CARGO_BIN_EXE_kestrel16_asm` is what the C++ tests reach for through
// the `KESTREL16_ASM_PATH` compile definition.

use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicUsize, Ordering};

const ORIGIN: u32 = 0x8000;
const SIZE: u32 = 0x1000;

/// A sentinel after the code makes every case length-exact: the image is padded
/// to its full size, so the tail would otherwise go unchecked.
const SENTINEL: u8 = 0xEE;

const TOOL: &str = env!("CARGO_BIN_EXE_kestrel16_asm");

/// A name no other case can take, so the cases can be assembled side by side.
fn unique(stem: &str) -> PathBuf {
    static NEXT: AtomicUsize = AtomicUsize::new(0);
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join("kestrel16_asm_tests");
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
    // Immediates. Without a suffix an immediate is a byte, because the 6502 the
    // processor starts in has byte-wide registers and the source has to say
    // when it has cleared the M flag; `.W` says so, `.B` says it again for a
    // reader. The index-register instructions are the exception: X and Y are
    // sixteen bits wide here, so their immediates are words unless the source
    // narrows them.
    expect("  LDA #$12", &[0xA9, 0x12]);
    expect("  LDA.W #$1234", &[0xA9, 0x34, 0x12]);
    expect("  LDA.B #$12", &[0xA9, 0x12]);
    expect("  LDX.B #$12", &[0xA2, 0x12]);
    expect("  LDX #$12", &[0xA2, 0x12, 0x00]);
    expect("  LDY.W #$1234", &[0xA0, 0x34, 0x12]);
    expect("  ADC.B #$01", &[0x69, 0x01]);
    expect("  REP #$30", &[0xC2, 0x30]);
    expect("  SEP #$20", &[0xE2, 0x20]);
    expect("  PEA $1234", &[0xF4, 0x34, 0x12]);
    expect("  COP #$02", &[0x02, 0x02]);
    expect("  BRK #$00", &[0x00, 0x00]);

    // Direct page and its indirections.
    expect("  LDA $10", &[0xA5, 0x10]);
    expect("  LDA $10,X", &[0xB5, 0x10]);
    expect("  LDX $10,Y", &[0xB6, 0x10]);
    expect("  LDA ($10)", &[0xB2, 0x10]);
    expect("  LDA ($10,X)", &[0xA1, 0x10]);
    expect("  LDA ($10),Y", &[0xB1, 0x10]);
    expect("  STA ($10),Y", &[0x91, 0x10]);
    expect("  LDA [$10]", &[0xA7, 0x10]);
    expect("  LDA [$10],Y", &[0xB7, 0x10]);
    expect("  STA [$10]", &[0x87, 0x10]);
    expect("  LDA $10,S", &[0xA3, 0x10]);
    expect("  LDA ($10,S),Y", &[0xB3, 0x10]);

    // Absolute, absolute long, and the jump forms that differ.
    expect("  LDA $1234", &[0xAD, 0x34, 0x12]);
    expect("  LDA $1234,X", &[0xBD, 0x34, 0x12]);
    expect("  LDX $1234,Y", &[0xBE, 0x34, 0x12]);
    expect("  JMP ($1234)", &[0x6C, 0x34, 0x12]);
    expect("  JMP ($1234,X)", &[0x7C, 0x34, 0x12]);
    expect("  JSR ($1234,X)", &[0xFC, 0x34, 0x12]);
    expect("  JMP $1234", &[0x4C, 0x34, 0x12]);
    expect("  JSR $1234", &[0x20, 0x34, 0x12]);
    expect("  JML [$1234]", &[0xDC, 0x34, 0x12]);
    expect("  LDA $7E:$2100", &[0xAF, 0x00, 0x21, 0x7E]);
    expect("  STA $7E:$2100", &[0x8F, 0x00, 0x21, 0x7E]);
    expect("  LDA.L $1234", &[0xAF, 0x34, 0x12, 0x00]);
    expect("  LDA $123456,X", &[0xBF, 0x56, 0x34, 0x12]);
    expect("  JSL $C01234", &[0x22, 0x34, 0x12, 0xC0]);
    expect("  JML $C01234", &[0x5C, 0x34, 0x12, 0xC0]);

    // Implied and stack instructions.
    expect("  PHB", &[0x8B]);
    expect("  PLB", &[0xAB]);
    expect("  PHD", &[0x0B]);
    expect("  PHK", &[0x4B]);
    expect("  TCD", &[0x5B]);
    expect("  XCE", &[0xFB]);
    expect("  RTL", &[0x6B]);
    expect("  WAI", &[0xCB]);
    expect("  PHX", &[0xDA]);
    expect("  ASL", &[0x0A]);

    // Block moves take two banks, destination first.
    expect("  MVN $7E,$7F", &[0x54, 0x7E, 0x7F]);
    expect("  MVP $01,$02", &[0x44, 0x01, 0x02]);
}

#[test]
fn labels_and_branches() {
    // A branch back to its own line is four bytes from the opcode: the offset
    // counts from the instruction after the branch.
    expect("again:\n  LDA $10\n  BNE again", &[0xA5, 0x10, 0xD0, 0xFC]);
    // The long branch reaches sixteen bits without help.
    expect(
        "again:\n  LDA $10\n  BRL again",
        &[0xA5, 0x10, 0x82, 0xFB, 0xFF],
    );
    // A near branch that cannot reach becomes the inverse branch over a JMP,
    // which is how a 6502 assembler has to express it.
    let mut far = vec![0xF0, 0x03, 0x4C, 0xCD, 0x80];
    far.extend(std::iter::repeat_n(0xEA, 200));
    far.push(0x60);
    expect("  BNE far\n  .fill 200, $EA\nfar:\n  RTS", &far);
    // BRA has no inverse, so a BRA that cannot reach becomes a plain JMP: the
    // inverse-branch expansion would have emitted LDY here.
    let mut bra = vec![0x4C, 0xCB, 0x80];
    bra.extend(std::iter::repeat_n(0xEA, 200));
    bra.push(0x60);
    expect("  BRA far\n  .fill 200, $EA\nfar:\n  RTS", &bra);
    // A forward reference that has to settle on the wide form still converges.
    expect(
        "  .word later\n  NOP\nlater:\n  RTS",
        &[0x03, 0x80, 0xEA, 0x60],
    );
}

#[test]
fn dialect() {
    // Data directives carry bytes through untouched.
    expect(
        "  .byte $41, $42\n  .text \"HI\"\n  .byte 0",
        &[0x41, 0x42, 0x48, 0x49, 0x00],
    );
    // A mnemonic with a width suffix is still recognised in the label field, so
    // `LDA.B` at the start of a line is not mistaken for a label.
    expect(
        "  LDA.B #$01\n  LDA.W #$0203",
        &[0xA9, 0x01, 0xA9, 0x03, 0x02],
    );
    // `LDA.B` as a column-0 mnemonic, which is the same test on the other side.
    expect("LDA.B #$01", &[0xA9, 0x01]);
    // The bank-qualified and long forms both settle on the long mode.
    expect("  .word $1234\n  .byte 'A'+1", &[0x34, 0x12, 0x42]);
}

#[test]
fn refusals() {
    // A byte immediate that does not fit is a missing `.W`, not a truncation:
    // carrying on would eat the opcode after it.
    expect_refused("  LDA.B #$1234", "immediate too wide for .B");
    expect_refused("start: WIBBLE #$12", "a mnemonic that does not exist");
    expect_refused("  WIBBLE #$12", "an identifier where an opcode should be");
    expect_refused("  .byte $", "empty hexadecimal number");
}

/// An operand that does not fit the form it ends up in is a diagnostic, not a
/// truncated encoding. `STX $1234,Y` has no absolute,Y form, so emitting the low
/// byte would assemble a store somewhere else entirely.
#[test]
fn an_operand_too_wide_for_its_form_is_refused() {
    for (source, expected) in [
        ("  STX $1234,Y\n", "does not fit direct page,Y"),
        ("  LDA.W #$123456\n", "does not fit immediate"),
        ("  STA ($1234)\n", "does not fit"),
    ] {
        let case = unique("too-wide");
        let file = case.with_extension("s");
        std::fs::write(&file, source).expect("a source file");
        let output = Command::new(TOOL)
            .arg(&file)
            .args(["--origin", "0x8000", "--size", "16", "--bin"])
            .arg(case.with_extension("bin"))
            .output()
            .expect("the assembler to run");
        assert_eq!(output.status.code(), Some(1), "{source}");
        let message = String::from_utf8_lossy(&output.stderr);
        assert!(
            message.contains(expected),
            "{source} should have said {expected:?}, said {message:?}"
        );
    }
}

/// `PEI` takes a direct page address and reads its pointer from there. It had no
/// bare-operand form in the parser at all, so the instruction the manual lists
/// could not be assembled.
#[test]
fn pei_takes_a_direct_page_operand() {
    let case = unique("pei");
    let file = case.with_extension("s");
    std::fs::write(&file, "  PEI $12\n  PEA $1234\n").expect("a source file");
    let binary = case.with_extension("bin");
    let output = Command::new(TOOL)
        .arg(&file)
        .args(["--origin", "0x8000", "--size", "8", "--fill", "0", "--bin"])
        .arg(&binary)
        .output()
        .expect("the assembler to run");
    assert!(output.status.success(), "{:?}", String::from_utf8_lossy(&output.stderr));
    let bytes = std::fs::read(&binary).expect("an image");
    assert_eq!(&bytes[..5], &[0xD4, 0x12, 0xF4, 0x34, 0x12], "PEI $12 ; PEA $1234");
}
