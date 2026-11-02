# How the code is put together

One emulator, two models, and two assemblers. Everything else is a ROM image.

```
          kestrel (CLI)   kestrel_emu (session)   kestrel_computers (window)
                 \               |                     /
                  \              |                    /
                        kestrel_machine
                        /             \
              devices (shared)      the two processors
                                     /              \
                            model1982 (6502)   model1992 (65816)
                                     \              /
                                roms/kestrel   roms/kestrel16
                                     ^              ^
                             kestrel_asm     kestrel16_asm
```

## The libraries

| Crate | What it is |
| --- | --- |
| `kestrel-machine` | The machine: both processors, the bus, the tube, the keyboard, the disk, the tape, and the ROMs assembled from source |
| `kestrel-asm`, `kestrel16-asm` | The two assemblers, as libraries and as the `kestrel_asm`/`kestrel16_asm` programs |
| `kestrel-cli` | `kestrel`, the command line, and `kestrel_emu`, the interactive emulator |
| `kestrel-app` | `kestrel_computers`, the window, in egui |

Apart from egui and the windowing it pulls in, there are no dependencies beyond
the standard library.

## The machine

`Machine` is built for a `Model`. The profile table in
`rust/crates/kestrel-machine/src/profile.rs` says what that model is, and the bus decodes against
it: RAM, the ROM windows, the cartridge, the tube's two arrays and the I/O page.

| File | Part |
| --- | --- |
| `rust/crates/kestrel-machine/src/profile.rs` | What each model is, in one table |
| `rust/crates/kestrel-machine/src/bus.rs` | The address decode, the I/O page and its states, the disk commands |
| `rust/crates/kestrel-machine/src/machine.rs` | The processor joined to the bus: frames, typing, reading the tube |
| `rust/crates/kestrel-machine/src/devices.rs` | The tube, the keyboard, the disk and the tape, parameterised by the profile |
| `rust/crates/kestrel-machine/src/cpu6502.rs` | The Kestrel's processor, whole |
| `rust/crates/kestrel-machine/src/cpu65816.rs` | The Kestrel 16's processor, whole |
| `rust/crates/kestrel-machine/src/roms.rs` | The ROMs, assembled from `roms/*.s` at start-up |

The two processors are not interchangeable: they are separate types, and the
`Cpu` enum the machine holds is decided once at power-on, so only the one the
model has is ever stepped. A fault in one cannot show up as a fault in the other.

### The two models

| | `model1982` | `model1992` |
| --- | --- | --- |
| Processor | 6502, 64 KiB | 65816, 16 MiB / 24-bit |
| Screen | 40 x 25 at `$8000` | 80 x 50 at `$008000` |
| I/O | `$8800` | `$A000` |
| Language | socket ROM at `$A000` | bank `$01` |
| Disk | 512 x 256 | 4096 x 512 |

Each model's map has exactly one home — `rust/crates/kestrel-machine/src/bus.rs` and
`rust/crates/kestrel-machine/src/bus.rs` — and `roms/kestrel/kestrel.inc` and
`roms/kestrel16/kestrel16.inc` repeat them for the ROMs. The pairs are changed
together, in the same commit.

## The ROMs

The languages are not Rust: they are `roms/kestrel/*.s` and `roms/kestrel16/*.s`,
assembled at build time by `kestrel_asm` and `kestrel16_asm` into headers of
bytes that the machine is given. A change to a ROM is a change to the machine,
and the machine tests prove it by typing at the machine and reading the screen
back.

## The tests

| Suite | What it proves |
| --- | --- |
| Where | Proves |
| --- | --- |
| `kestrel-asm` | The dialect the 6502 ROMs are written in |
| `kestrel-machine`, `cpu6502` | The 6502: addressing, arithmetic, flags, cycles, interrupts |
| `kestrel-machine`, `machine` | The Kestrel: its ROMs assemble, boot, and typing works |
| `kestrel16-asm` | Every 65816 addressing mode and width rule, byte for byte |
| `kestrel-machine`, `cpu65816` | The 65816: widths, banks, the direct page, arithmetic, cycles |
| `kestrel-machine`, `machine` | The Kestrel 16: its ROMs assemble, boot, and typing works |
| `kestrel-cli`, `samples` | Every program in `samples/` runs, and prints what its `.expected` file says |
| `kestrel-cli`, `emulator` | The emulator's front end: the model flag, switching models in a session, and the refusals |

`./start.sh test` runs all of them.
