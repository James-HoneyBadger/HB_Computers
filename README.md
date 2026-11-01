# HB Computers

Two complete computers, and the engines they run on.

- the **HB-1** — a personal computer from a 1982 that never happened: eleven
  language ROMs in one socket, a prompt, a 64K, a CRT and a disk measured in
  256-byte sectors; and
- the **HB-2** — its 1992 successor: a bus, a 640 × 480 framebuffer and a
  blitter, four channels of sampled audio, hardware sprites, and HB-16, a
  processor of the machine's own, with a desktop, a disk and a prompt.

Both are real programs, not screenshots. You can type at them, break them, and
come back to the same machine. Neither is an emulator: the HB-1 runs a native
implementation of each of its languages, and the HB-2 runs nine of them on its own
bus as well as an instruction set you can disassemble.

| | |
| --- | --- |
| [`docs/MACHINES.md`](docs/MACHINES.md) | What the two machines are, in one page |
| [`docs/hb1/README.md`](docs/hb1/README.md) | The HB-1 owner's manual |
| [`docs/hb2/README.md`](docs/hb2/README.md) | The HB-2 owner's manual |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | How the code is put together |

## Building

The engines need a C++20 compiler and CMake 3.20 or newer. The machine windows
need Qt 6 Widgets as well; a build host without it still gets every engine, the
headless HB-2 tool and most of the test suites.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`-DHB_BUILD_GUI=OFF` builds everything that does not need Qt.
`-DHB_WERROR=OFF` relaxes the warnings-as-errors policy.

## Running

```bash
./build/hb_computers              # the launcher: pick a machine
./build/hb_computers --hb1        # power on the HB-1
./build/hb_computers --hb2        # power on the HB-2
./build/hb_computers --machine HV.hb1      # restore the HB-1 from an image
./build/hb_computers --listing prog.s --hb2 # load a listing into the HB-2
```

The HB-1 boots into BASIC with a `READY.` prompt. Type a numbered line to build a
program, `RUN` to run it, or `CATALOG` at the prompt to see the disk. The ROM
selector in the header exchanges BASIC for any of the other ten languages; each
keeps its own program when you switch away.

The HB-2 needs a listing: open the **Program** page, write or paste a program, and
press **Run**. The programs it ships with are in
[`Examples/hb2/`](Examples/hb2/).

The HB-2 alone runs on a shell, which is how the manual's examples are checked:

```bash
./build/hb2 Examples/hb2/palette.s --ticks 4 --screen
./build/hb2 --tasks Examples/hb2/task_a.s Examples/hb2/task_b.s --ticks 6
./build/hb2 --map
```

A program that runs, halts and draws nothing is reported as a failure, because a
machine that runs and shows nothing is the bug this project exists to catch.

## What is in the tree

```
include/timewarp/   the engines and the widgets, as headers
src/                their implementations
app/                the launcher window and its main()
tools/hb2.cpp       the HB-2 without a window
tests/              one executable per suite, no external test framework
Examples/           the programs the machines ship with
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
