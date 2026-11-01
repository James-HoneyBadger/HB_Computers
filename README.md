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
assembled into a ROM image and executed. Neither machine has an interpreter
written for it in C++.

```
$ ./build/kestrel --model kestrel --type "PRINT 2+2"
*** KESTREL BASIC V2.0 ROM ***
64K RAM SYSTEM
(C) 1982 HONEYBADGER SYSTEMS

READY.
PRINT 2+2
4
READY.

$ ./build/kestrel --model kestrel16 --type "PRINT 2+2"
KESTREL 16 BASIC 2.0
READY> PRINT 2+2
4
```

## One machine, two models

`kestrel::Machine` is built for a `Model`, and a profile table says what that
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

A C++20 compiler and CMake 3.20. The window needs Qt 6 Widgets; without it the
machine, the tools and the tests still build.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Six suites: the two assemblers, the two processors, and the two machines booting
their ROMs and answering at the keyboard.

## Running

```bash
./build/kestrel --model kestrel --type "PRINT 2+2"   # on a shell
./build/kestrel --model kestrel16 --frames 20        # the 1992 machine
./build/kestrel_computers                            # the window
./build/kestrel_computers --model kestrel16          # the 1992 machine's window
```

## What is in the tree

```
include/kestrel/          the machine, its devices, and the two models
src/kestrel/              their implementations
app/                      the window's main()
tools/                    kestrel_asm, kestrel16_asm and the command line
roms/kestrel/             the Kestrel's 6502 machine code
roms/kestrel16/           the Kestrel 16's 65816 machine code
tests/                    one executable per suite, no external test framework
docs/                     the manuals
```

## The manuals

| Book | What it covers |
| --- | --- |
| [Kestrel Owner's Manual](docs/kestrel/README.md) | The 6502, the map, the I/O page, the KERNAL, Kestrel DOS, the language, the errors, the limits |
| [Kestrel 16 Owner's Manual](docs/kestrel16/README.md) | The 65816, the banks, the KERNAL's two tables, the language, the limits |
| [The tools](docs/TOOLS.md) | Both assemblers and their dialect, the command line, the window, the build |
| [Architecture](docs/ARCHITECTURE.md) | How the code is put together, for people changing it |
