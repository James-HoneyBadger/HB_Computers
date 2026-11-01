# Architecture

This repository is the [Kestrel and Kestrel 16 machines](MACHINES.md) without the IDE they
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
  kestrel/                the 6502 and the Kestrel's map
  kestrel16/                the 65816 and the Kestrel 16's map
  ui/                 the Qt widgets both machines are made of
src/                  the implementations, mirroring include/
app/                  the launcher: one window, two machines
tools/kestrel16_asm.cpp      the 65816 assembler that builds the Kestrel 16's ROMs
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
kestrel_computers   kestrel (CLI)
      |              |
      +------+-------+
             |
      hb_launcher ── hb_ui  ─────────┐   Qt Widgets
                       /      \       │
              hb_canvas_qt   kestrel_machine
                     |             │
                     |       (both processors and both ROM sets)
                     |
 hb_canvas       hb_lang        Qt-free
                   |
      hb_expr  hb_audio  hb_io  hb_scheme
```

| Library | What it owns | Qt |
| --- | --- | --- |
| `hb_expr` | The expression lexer, parser and evaluator used by the line-loop languages | no |
| `hb_scheme` | The Scheme reader, value type, machine and builtins | no |
| `hb_audio` | The mixer, the note table and the 22 050 Hz sample clock | no |
| `hb_io` | The serial port and the gamepad reader | no |
| `hb_lang` | The interpreter loop, the value type, dispatch and one executor per language | no |
| `hb_canvas` | `CanvasCommand` and the thread-safe `CanvasCommandQueue` | no |
| `kestrel_machine` | One machine, two models: both processors, the bus, the tube, the keyboard, the disk and the tape | no |
| `hb_canvas_qt` | The bridge that drains the queue on the GUI thread | Core |
| `hb_ui` | Both machine windows and the widgets they are made of | Widgets |
| `hb_launcher` | The front door: a window naming both machines | Widgets |

`kestrel_machine` links nothing but itself: both machines' languages are machine code
in ROM, so it needs a processor and a bus rather than an interpreter. `hb_ui`
links `hb_lang` directly for the Kestrel's prompt and PROGRAM page.

A build host with no Qt still gets every library above the line, the `hb2` tool and
seven of the ten test suites. `-DHB_BUILD_GUI=OFF` asks for exactly that.

## The Kestrel

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
- **`HbDos`** is Kestrel DOS 1.0 — a widget-free class over one JSON image, with
  rollback on a failed write.
- **`HbBbs`** is the WARP BOARD, the board on the other end of the line.
- **`MachineVoice`** is the machine's own sound, through the mixer.
- **`CrtOverlay`** ([`crt_overlay.hpp`](../include/timewarp/ui/crt_overlay.hpp)) is
  the glass: scanlines, vignette, curvature, flicker and phosphor, drawn over
  whichever widget it covers.

A run goes through `NativeRunner`; a session goes through `ReplRunner`. Both feed
the same `CanvasCommandQueue`, which `hb_canvas_qt` drains on the GUI thread into a
`TurtleCanvas`.

## The Kestrel 16

The machine is one Qt-free library. `Machine` owns the bus, the processor, the
sections of the map. Each model's map has exactly one home — `kestrel/map.hpp` and
`kestrel16/map.hpp` — and `roms/kestrel/kestrel.inc` and `roms/kestrel16/kestrel16.inc` repeat them for the
ROMs; the pairs are changed together.

The two machines are one program. `timewarp::Machine` is built for a `Model`, and
the profile table in `machine.cpp` says what that model is: its clock, its memory,
where its screen and its registers are, how big its ROMs are, and how its disk is
divided. Everything that is the same ten years apart is shared code.

| File | Part |
| --- | --- |
| `src/machine.cpp` | The profile table, the decoded address space, the I/O page, the frame, the disk commands |
| `src/devices.cpp` | The tube, the keyboard, the disk and the tape, parameterised by the profile |
| `src/kestrel/cpu6502.cpp` | The Kestrel's processor, whole |
| `src/kestrel16/cpu65816.cpp` | The Kestrel 16's processor, whole |

What is *not* shared is the two processors and the two ROM images. Each
processor is the part its machine really had, and the machine never substitutes
one for the other: a 6502 machine has no 65816 in it, and asking a `Machine` for
the processor it does not have is a compile error, not a runtime check.

The KERNAL and the language are not C++ at all: they are `roms/kestrel16/kernal.s` and
`roms/kestrel16/basic.s`, assembled by `kestrel16_asm` at build time and mounted with
`mountKernal` and `mountBasic`.

| `lexer_tests`, `evaluator_tests` | The expression engine |
| `scheme_tests` | The Scheme machine |
| `lang_tests` | Every executor, including the shipped examples |
| `audio_tests` | The mixer |
| `canvas_queue_tests`, `canvas_bridge_tests` | The queue and its Qt bridge |
| `syntax_highlighter_tests` | The editor's colouring |
| `kestrel16_asm_tests`, `kestrel16_cpu_tests`, `kestrel16_machine_tests` | The 65816 assembler, the processor, and the Kestrel 16 model booting its ROMs |
| `retro_computer_tests` | The Kestrel: ROM socket, screen modes, direct mode, Kestrel DOS, memory, boot, images, configuration |

`ctest --test-dir build` runs all of them. The two window suites need a display or
`QT_QPA_PLATFORM=offscreen`, which is what the tests set for themselves.
