# Architecture

This repository is the [HB-1 and HB-2 machines](MACHINES.md) without the IDE they
were first built inside. Nothing here is a stub: the two computers, every engine
they run, and the tests that pin them.

## The tree

```
include/timewarp/     headers, included as "timewarp/..."
  core/               small shared utilities (charconv, Qt name tables)
  expr/               the expression lexer and evaluator
  scheme/             the Scheme machine
  audio/              the mixer and the note table
  io/                 serial ports and the gamepad reader
  lang/               the interpreter loop, the value type, one executor per language
  graphics/           the recording host a test draws into
  canvas/             canvas commands and the thread-safe command queue
  hb2/                the HB-2: the 65816, the bus, the tube, the disk, the tape
  ui/                 the Qt widgets both machines are made of
src/                  the implementations, mirroring include/
app/                  the launcher: one window, two machines
tools/hb2asm.cpp      the 65816 assembler that builds the HB-2's ROMs
tests/                one executable per suite, no external test framework
Examples/             the programs the machines ship with
docs/                 the manuals
```

The include path is the repository's `include/`, so a source says
`#include "timewarp/ui/retro_computer.hpp"` and nothing else has to know where the
tree is.

## The layers

A library is one answer, and the arrows only point one way.

```
hb_computers  hb2 (CLI)
      |         |
      +----+----+
           |
      hb_launcher
           |
        hb_ui  ──────────────┐   Qt Widgets
        /   \                │
 hb_canvas_qt  hb_hb2        │
      |          |           │
      |       hb_lang        │
      |      /   |   \       │
      |  hb_expr hb_audio hb_io
      |          |           │
 hb_canvas   hb_scheme ──────┘   (Qt-free)
```

| Library | What it owns | Qt |
| --- | --- | --- |
| `hb_expr` | The expression lexer, parser and evaluator used by the line-loop languages | no |
| `hb_scheme` | The Scheme reader, value type, machine and builtins | no |
| `hb_audio` | The mixer, the note table and the 22 050 Hz sample clock | no |
| `hb_io` | The serial port and the gamepad reader | no |
| `hb_lang` | The interpreter loop, the value type, dispatch and one executor per language | no |
| `hb_canvas` | `CanvasCommand` and the thread-safe `CanvasCommandQueue` | no |
| `hb_hb2` | The HB-2: the 65816, the bus, the tube, the keyboard, the disk, the tape | no |
| `hb_canvas_qt` | The bridge that drains the queue on the GUI thread | Core |
| `hb_ui` | Both machine windows and the widgets they are made of | Widgets |
| `hb_launcher` | The front door: a window naming both machines | Widgets |

`hb_hb2` links nothing but itself: its languages are machine code in ROM, so it
needs a processor and a bus rather than an interpreter. `hb_ui` links `hb_lang`
directly for the HB-1's prompt and PROGRAM page.

A build host with no Qt still gets every library above the line, the `hb2` tool and
seven of the ten test suites. `-DHB_BUILD_GUI=OFF` asks for exactly that.

## The HB-1

`RetroComputerWindow` ([`retro_computer.hpp`](../include/timewarp/ui/retro_computer.hpp))
is one window with a front panel, a CRT, a status line and a prompt. It is split by
what each part does:

| File | Part |
| --- | --- |
| `retro_computer_ui.cpp` | The front panel, the screen, the tabs and the status line |
| `retro_computer_basic.cpp` | The line editor, `AUTO` and renumbering |
| `retro_computer_machine.cpp` | The ROM, the runners, the canvas and the machine images |
| `retro_computer_dos.cpp` | The prompt's half of the disk words |
| `retro_computer_boot.cpp` | Power, self test and the frame tick |
| `retro_computer_settings.cpp` | Persistence and the CONFIG dialog |
| `retro_computer_support.cpp` | Helpers shared by the above |

Under it:

- **`HbMemory`** is the 64K: one address space with screen cells, colour
  attributes, I/O registers, and a scroll that moves memory rather than a paint
  offset.
- **`HbTube`** paints that memory: a cursor, a scrollback and a character set.
- **`HbDos`** is HB DOS 1.0 — a widget-free class over one JSON image, with
  rollback on a failed write.
- **`HbBbs`** is the WARP BOARD, the board on the other end of the line.
- **`MachineVoice`** is the machine's own sound, through the mixer.
- **`CrtOverlay`** ([`crt_overlay.hpp`](../include/timewarp/ui/crt_overlay.hpp)) is
  the glass: scanlines, vignette, curvature, flicker and phosphor, drawn over
  whichever widget it covers.

A run goes through `NativeRunner`; a session goes through `ReplRunner`. Both feed
the same `CanvasCommandQueue`, which `hb_canvas_qt` drains on the GUI thread into a
`TurtleCanvas`.

## The HB-2

The machine is one Qt-free library. `Machine` owns the bus, the processor, the
sections of the map. The address map has exactly one home —
[`map.hpp`](../include/timewarp/hb2/map.hpp) — and `roms/hb2/hb2.inc` repeats it for
the ROMs; the two are changed together.

| File | Part |
| --- | --- |
| `cpu65816.cpp` | The processor: emulation and native modes, the M and X widths, the banks, the direct page, the cycle counts |
| `machine.cpp` | The decoded address space, the frame interrupt, the I/O page |
| `video.cpp` | The 80 x 50 cells and the attribute byte each |
| `keyboard.cpp` | The key queue the ROM reads |
| `disk.cpp` | A sector store: 4096 sectors of 512 bytes |
| `tape.cpp` | A byte stream with a play head |

The KERNAL and the language are not C++ at all: they are `roms/hb2/kernal.s` and
`roms/hb2/basic.s`, assembled by `hb2asm` at build time and mounted with
`mountKernal` and `mountBasic`.

| `lexer_tests`, `evaluator_tests` | The expression engine |
| `scheme_tests` | The Scheme machine |
| `lang_tests` | Every executor, including the shipped examples |
| `audio_tests` | The mixer |
| `canvas_queue_tests`, `canvas_bridge_tests` | The queue and its Qt bridge |
| `syntax_highlighter_tests` | The editor's colouring |
| `hb2_asm_tests`, `hb2_cpu_tests`, `hb2_machine_tests` | The 65816 assembler, the processor, and the machine booting its ROMs |
| `retro_computer_tests` | The HB-1: ROM socket, screen modes, direct mode, HB DOS, memory, boot, images, configuration |

`ctest --test-dir build` runs all of them. The two window suites need a display or
`QT_QPA_PLATFORM=offscreen`, which is what the tests set for themselves.
