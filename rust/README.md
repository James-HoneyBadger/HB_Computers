# The Rust port

`Rust and egui`, replacing the C++20 and Qt 6 of the tree above — done the only
way a rewrite of this size survives: one crate at a time, each with the tests that
prove it, and the C++ tree kept until the port can do everything it does.

The port is far enough along to boot both machines and answer BASIC:

```bash
./start.sh build                              # assembles the ROMs the port loads

cd rust
cargo run --offline -p kestrel-cli -- --type 'PRINT 2+2*3'
cargo run --offline -p kestrel-cli -- --model kestrel16 --type 'PRINT 1000*20'
cargo run --offline -p kestrel-cli -- --file ../samples/kestrel/03-fibonacci.bas --type RUN
cargo test --offline
```

| Crate | Ports | State |
| --- | --- | --- |
| `kestrel-machine` | `include/kestrel/`, `src/kestrel/` — profile, devices, the bus decode, the I/O page, the disk commands | done |
| `kestrel-machine` | `model1982/cpu6502.cpp` | done, `cpu6502.rs` |
| `kestrel-machine` | `model1992/cpu65816.cpp` | done, `cpu65816.rs` |
| `kestrel-machine` | `src/kestrel/machine.cpp` | done, `machine.rs` |
| `kestrel-cli` | `tools/kestrel.cpp` | done, `kestrel-cli` |
| `kestrel-cli` | `tools/kestrel_emu.cpp` — the interactive one | next |
| `kestrel-app` | `app/main.cpp` and `window.cpp`, in egui | last |

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
  processor, KERNAL and language ROM — agreeing screenful by screenful.

The images are assembled by the C++ build; a Rust-only checkout has no ROMs, so
those tests say so rather than passing quietly.

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
