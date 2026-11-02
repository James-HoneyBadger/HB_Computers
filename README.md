# The Kestrel

Two home computers from a 1982 that never happened, and the one emulator that
runs both.

- the **Kestrel** — a 6502 at 1 MHz, 32 KiB of RAM behind a banner that claims
  64K, a 40 × 25 tube, a disk measured in 256-byte sectors, an 8 KiB KERNAL and a
  16 KiB language socket; and
- the **Kestrel 16** — its 1992 successor: a 65816 at 8 MHz, a megabyte of banked
  memory, an 80 × 50 tube, and Kestrel 16 BASIC in a 64 KiB bank of its own.

Both are real machines, not pictures of machines. The host supplies a processor, a
bus, a tube, a keyboard and a disk; everything on the glass is machine code
assembled into a ROM image and executed -- on both machines by an assembler in
this tree, from the sources beside them. Neither machine has an interpreter
written for it in Rust.

```
$ ./start.sh cli --model kestrel --type "PRINT 2+2"
*** KESTREL BASIC V2.0 ROM ***
64K RAM SYSTEM
(C) 1982 HONEYBADGER SYSTEMS

READY.
PRINT 2+2
4
READY.

$ ./start.sh cli --model kestrel16 --type "PRINT 2+2"
KESTREL 16 BASIC 2.0
READY.
PRINT 2+2
4
```

## One machine, two models

`Machine` is built for a `Model`, and a profile table says what that
model is: its clock and its frame, how much RAM it answers for, where its screen
and its registers are, how big its ROMs are and how its disk is divided. The two
processors are the parts the machines really had, and each is used whole.

| | Kestrel (1982) | Kestrel 16 (1992) |
| --- | --- | --- |
| Processor | MOS 6502, 1 MHz | WDC 65C816, 8 MHz |
| Memory | 64 KiB | 16 MiB of address space, 1 MiB of RAM |
| Screen | 40 × 25 cells | 80 × 50 cells |
| Language | a 16 KiB socket ROM | a 64 KiB bank of its own |
| Disk | 512 sectors of 256 bytes | 4096 sectors of 512 bytes |
| Numbers | five-byte floating point | sixteen-bit integers |

## Building

A Rust toolchain, and a desktop for the window. Everything is in
[rust/](rust); the ROMs need no build of their own because the programs assemble
them from [roms/](roms) when they start.

```bash
cd rust
cargo build --release
cargo test
```

Forty-three tests: the two assemblers, the two processors, the bus and its
devices, both machines booting their assemblers' ROMs and answering at the
keyboard, every sample program in [samples/](samples), and the emulator's own
front end.

`./start.sh` builds what is out of date and starts a machine:

```bash
./start.sh            # the Kestrel's window
./start.sh kestrel16  # the Kestrel 16's window
./start.sh emu        # the interactive emulator
./start.sh cli --type "PRINT 2+2"
./start.sh test       # every suite
```

### In a virtual machine

A `Vagrantfile` in the root makes an Ubuntu 22.04 build machine -- a Rust
toolchain and an X server -- and builds and tests the tree as the `vagrant`
user:

```bash
vagrant up      # provision, build, and run the suites
vagrant ssh     # the tree is at /vagrant
```

## Running

```bash
./start.sh cli --model kestrel --type "PRINT 2+2"   # on a shell
./start.sh cli --model kestrel16 --frames 20        # the 1992 machine
./start.sh                            # the window
./start.sh --model kestrel16          # the 1992 machine's window
./start.sh emu                                  # either machine, interactively
./start.sh cli --model kestrel --file samples/kestrel/02-sieve.bas --type RUN
```

## What is in the tree

```
include/kestrel/          the machine, its devices, and the two models
src/kestrel/              their implementations
app/                      the window's main()
tools/                    kestrel_asm, kestrel16_asm, the command line and the interactive emulator
roms/kestrel/             the Kestrel's 6502 machine code
roms/kestrel16/           the Kestrel 16's 65816 machine code
samples/                  programs to type, run and learn from, and their expected output
tests/                    one executable per suite, no external test framework
docs/                     the five books
start.sh                  build what is not built, then start a machine
Vagrantfile               the same in a clean Ubuntu 22.04 build machine
```

## The manuals

Four books, two references and the machines' own manuals; [the index](docs/README.md) says which to read when.

| Book | What it covers |
| --- | --- |
| [Instruction Manual](docs/instruction-manual.md) | The box: both machines, building, switching on, the keyboard, the window, the media, first sessions, troubleshooting |
| [User Guide](docs/users-guide.md) | The software: Kestrel BASIC, the monitor ROM, Kestrel DOS, Kestrel 16 BASIC, errors, worked examples, the samples |
| [Technical Reference](docs/technical-reference.md) | The hardware: both maps, the I/O pages, the colours, the bus, interrupts, the disk and tape formats, the ROM images, the processors |
| [Programmer's Guide](docs/programmers-guide.md) | Writing for the machines and changing the emulator: the assemblers' pitfalls, the KERNAL's tables, the emulator's API, the tests, the build |
| [The tools](docs/TOOLS.md) | Both assemblers, the command line, the window, the build targets |
| [Architecture](docs/ARCHITECTURE.md) | How the code is put together, for people changing it |

The machines' own books are the long form of the language and hardware chapters:
[the Kestrel Owner's Manual](docs/kestrel/README.md) and
[the Kestrel 16 Owner's Manual](docs/kestrel16/README.md).
