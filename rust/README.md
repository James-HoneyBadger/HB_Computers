# The Rust port

`Rust and egui`, replacing the C++20 and Qt 6 of the tree above — done the only
way a rewrite of this size survives: one crate at a time, each with the tests that
prove it, and the C++ tree kept until the port can do everything it does.

The port is far enough along to boot both machines and answer BASIC:

```bash
cd rust
cargo run --offline --bin kestrel -- --type 'PRINT 2+2*3'
cargo run --offline --bin kestrel -- --model kestrel16 --type 'PRINT 1000*20'
cargo run --offline --bin kestrel -- --file ../samples/kestrel/03-fibonacci.bas --type RUN
cargo run --offline --bin kestrel_emu                 # the interactive one
cargo run --offline --bin kestrel_computers           # the window
cargo test --offline
```

The window is the one place the port cannot be proved by a diff: it is proved by
being opened and typed at. It draws the tube from the attribute bytes, takes the
keys a person presses, and drops files onto the machine -- a cartridge, a socket
ROM or a tape, decided by the file's size.

`--shot FILE` photographs the window and leaves, as the Qt one did, so a headless
job can keep a picture of the machine:

```bash
xvfb-run -a cargo run --offline --bin kestrel_computers -- --shot shot.png
```

What it has not carried over from the Qt window, and why:

- menus for the disk and the tape: the file picker was Qt's, and a dropped file
  is a better answer on a modern desktop, so `Tape > Stop` is the whole menu;
- settings that survive a restart: eframe can persist them, and nothing yet
  needs to.

| Crate | Ports | State |
| --- | --- | --- |
| `kestrel-machine` | `include/kestrel/`, `src/kestrel/` — profile, devices, the bus decode, the I/O page, the disk commands | done |
| `kestrel-machine` | `model1982/cpu6502.cpp` | done, `cpu6502.rs` |
| `kestrel-machine` | `model1992/cpu65816.cpp` | done, `cpu65816.rs` |
| `kestrel-machine` | `src/kestrel/machine.cpp` | done, `machine.rs` |
| `kestrel-cli` | `tools/kestrel.cpp` | done, the `kestrel` binary |
| `kestrel-cli` | `tools/kestrel_emu.cpp` | done, the `kestrel_emu` binary |
| `kestrel-app` | `app/main.cpp` and `model1982/window.cpp`, in egui | done, the `kestrel_computers` binary |
| `kestrel-asm` | `tools/kestrel_asm.cpp` | done, the `kestrel_asm` binary; the same bytes out as the C++ tool for all three ROMs |
| `kestrel16-asm` | `tools/kestrel16_asm.cpp` | done, the `kestrel16_asm` binary; the same bytes out as the C++ tool for both ROMs |

## What proves it

Three kinds of test, in rising order of strength:

- **the ported suites.** `cpu6502.rs` and `cpu65816.rs` carry the C++ cores'
  unit tests, ported rather than summarised — addressing, arithmetic and flags,
  the decimal modes, the stack, interrupts, cycle counts.
- **the ported tables.** Each core's opcode map is generated from the C++'s, so
  an opcode is in both trees or in neither, at the same cycle count.
- **the samples** (`crates/kestrel-cli/tests/samples.rs`). All twenty-four sample
  programs in `samples/` are typed at the Rust machine and checked against the
  same `.expected` files the C++ suite uses. That is the whole machine — bus,
  processor, KERNAL and language ROM — agreeing screenful by screenful;
- **the tools** (`crates/kestrel-cli/tests/emulator.rs`). The interactive
  emulator's session commands, asked of the built binary, as the C++
  `kestrel_emu_tests` asks them.
- **the assembler** (`crates/kestrel-asm`). The C++ suite in
  `tests/kestrel_asm_tests.cpp` is ported, and the binary is then run over the
  three ROM sources and its image, symbol table, listing and header compared
  byte for byte with the ones the C++ build generated.

The images the port loads are still assembled by the C++ build; a Rust-only
checkout has no ROMs, so those tests say so rather than passing quietly.
`crates/kestrel-asm` now assembles the same three ROMs to the same bytes, so
which tool produces them no longer matters.

## When the two trees disagree

`cargo run --example differential` prints a per-frame hash of the machine —
registers, all of bank-zero RAM and the tube. The counterpart is thirty lines of
C++ linked against `build/libkestrel_machine.a` printing the same line, and
`diff` names the first frame that differs. That found the 6502's missing `INC`
and `DEC` flags: everything agreed until one instruction, where `INC $20` left
the zero flag alone, `BNE` fell through, and a line of BASIC printed nothing.

## Reading it

The port follows the original file by file, so the two can be read side by side:
`profile.rs` is `makeProfile`, `devices.rs` is `devices.cpp`, `bus.rs` is the
decode in `machine.cpp`, `machine.rs` is what is left of it. The numbers in the
profile are asserted against the C++ table's values, so a drift in either shows up
as a failing test rather than as a machine that behaves differently.
