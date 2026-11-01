# HB Computers

Two complete computers, and the engines they run on.

- the **HB-1** — a personal computer from a 1982 that never happened: a 6502 at
  1 MHz, a 64K, a 40 x 25 tube, a disk measured in 256-byte sectors, an 8 KiB
  KERNAL and a language ROM in the socket -- real 6502 machine code, assembled
  from `roms/hb1/` and run by the machine, not by the host; and
- the **HB-2** — its 1992 successor: a 65816 at 8 MHz, a megabyte of banked
  memory, an 80 x 50 tube, and HB BASIC 2.0 in a sixty-four kilobyte ROM bank of
  its own, with the KERNAL beside it.

Both are real programs, and they are one program: `timewarp::Machine` is built for
a model, and the profile table says what that model is. They are also the same
kind of machine:
the host supplies a processor, memory, a tube, a keyboard and a disk, and the
languages are machine code in ROM. The interpreter that answers `READY.` on the
HB-1 is 6502 code in the socket; the one that answers `READY` on the HB-2 is
65816 code in bank one.

Both are being rebuilt from the processor up; [the HB-1 firmware
page](docs/hb1/firmware.md) and [the HB-2 manual](docs/hb2/README.md) record what
the ROMs contain today and where the edge of each language is.

| | |
| --- | --- |
| [`docs/MACHINES.md`](docs/MACHINES.md) | What the two machines are, in one page |
| [`docs/hb1/README.md`](docs/hb1/README.md) | The HB-1 owner's manual |
| [`docs/hb2/README.md`](docs/hb2/README.md) | The HB-2 owner's manual |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | How the code is put together |

## Building

The engines need a C++20 compiler and CMake 3.20 or newer. The machine windows
need Qt 6 Widgets as well; a build host without it still gets every engine, the
the headless tools and most of the test suites.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`-DHB_BUILD_GUI=OFF` builds everything that does not need Qt.
`-DHB_WERROR=OFF` relaxes the warnings-as-errors policy.

## Running

```bash
./build/hb_computers              # the launcher
./build/hb_computers --hb1        # power on the HB-1
./build/hb_computers --machine HV.hb1      # restore the HB-1 from an image
```

The HB-1 flashes its banner at the top of the glass and leaves you at `READY.`
with a block cursor — you type straight at the machine, not into a field below it.
The disk has the machine's nine programs on it: `CATALOG` lists them,
`LOAD "TUBE WRITER"` puts one in memory, `RUN` sets it going. `F1`–`F9` are the
machine's function keys (`LIST`, `NEW`, `CATALOG`, `LOAD`, `SAVE`, graphics, text,
setup, run), and the key row along the bottom of the case clicks the same words.
A program that draws switches the display to the graphics screen; `F7` brings the
text back.

One executable boots either model, which is the point of the merge:

```bash
./build/hb_computer --model hb1 --type "PRINT 2+2"
./build/hb_computer --model hb2 --type "PRINT 2+2"
./build/hb_computer --model hb2 --frames 20 --speed 8
```

The HB-2 has no window yet; its model boots its own ROMs under
`hb2_machine_tests`, which types at it and reads the screen back.

Its ROMs are assembled from `roms/hb2/` by `hb2asm`, which is also a suite of its
own: `hb2_asm_tests` writes out every encoding the dialect has by hand.

## What is in the tree

```
include/timewarp/   the engines and the widgets, as headers
src/                their implementations
app/                the launcher window and its main()
tools/hb2asm.cpp    the 65816 assembler that builds both machines' ROMs
tests/              one executable per suite, no external test framework
Examples/           the programs the machines ship with
roms/hb1/ roms/hb2/ the machines' own machine code, assembled at build time
docs/               the manuals
```

The libraries, their dependencies and the parts of each machine are described in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Where this came from

The machines were built inside **Time Warp Studio**, an IDE with eleven languages
and two computers in it. This repository is the extraction of the computers: the
engines they run, the windows they are shown in, their tests, their examples and
their manuals, with the IDE's editor, docks, lessons, database and automation left
behind.

It was taken from `James-HoneyBadger/Time_Warp_Studio` at commit
`0cb0527b8329eca749f66fad8df371ea21a379ff` (2026-10-31). The machine sources are
kept as they were, so a fix can still travel between the two trees with a diff; the
build, the launcher and the manuals are this repository's own.

## Licence

MIT. See [`LICENSE`](LICENSE). Copyright © 2025 Honey Badger Universe.
