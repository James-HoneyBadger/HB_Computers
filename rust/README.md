# The Rust port

This is the tree: `Rust and egui`, which replaced the C++20 and Qt 6 the machine
was first written in. The port was done the only way a rewrite of this size
survives -- one crate at a time, each with the tests that prove it -- and the C++
tree was kept until the port could do everything it did, then deleted. Its last
state is in the history, where `git log --follow` will find it.

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

`--shot FILE` photographs the window and leaves, so a headless job can keep a
picture of the machine:

```bash
xvfb-run -a cargo run --offline --bin kestrel_computers -- --shot shot.png
```

Two deliberate simplifications, so nobody goes looking for them:

- there are no disk or tape file dialogs: a file dropped on the window is the
  modern answer, so `Tape > Stop` is the whole menu;
- nothing survives a restart: eframe can persist settings, and nothing yet needs
  to.

| Crate | Ported from | State |
| --- | --- | --- |
| `kestrel-machine` | the machine, its devices, the bus and the two processors | done |
| `kestrel-asm`, `kestrel16-asm` | the two assemblers | done, and byte-identical on all five ROMs |
| `kestrel-cli` | the command line and the interactive emulator | done |
| `kestrel-app` | the window | done |

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
  the C++ suite's tests are ported, and the binary is then run over the
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

The port was made file by file against the original, and the shape is still
visible: `profile.rs` is the model table, `devices.rs` the four devices, `bus.rs`
the address decode and the I/O page, `machine.rs` the processor joined to it.
Each module's header says what it is and, where it matters, why it is that way.

The numbers the machine's behaviour rests on are asserted in tests -- the profiles
against the documented figures, the opcode maps against the assemblers, the
machines against the sample programs' `.expected` files -- so a drift shows up as
a failing test rather than as a machine that behaves differently.
