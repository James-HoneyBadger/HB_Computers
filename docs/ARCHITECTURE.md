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
  hb2/                the HB-2: bus, processor, video, sound, DOS, shell, HB-OS
  ui/                 the Qt widgets both machines are made of
src/                  the implementations, mirroring include/
app/                  the launcher: one window, two machines
tools/hb2.cpp         the HB-2 without a window
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
| `hb_hb2` | The HB-2: bus, HB-16, video, sound, DOS, shell, HB-OS, monitor, modules, tape | no |
| `hb_canvas_qt` | The bridge that drains the queue on the GUI thread | Core |
| `hb_ui` | Both machine windows and the widgets they are made of | Widgets |
| `hb_launcher` | The front door: a window naming both machines | Widgets |

`hb_hb2` links `hb_lang` because the HB-2's ROM socket runs nine of the same
engines on its own bus. `hb_ui` links `hb_lang` directly for the HB-1's prompt and
PROGRAM page.

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
video and audio chips, the clock, the peripherals, the tape and the ROM socket. The
address map has exactly one home — [`devices.inc`](../include/timewarp/hb2/devices.inc)
— and the bus routes with it, the monitor prints it and the manual was written from
it.

| File | Part |
| --- | --- |
| `bus.cpp` | The decoded address space and the refusal at an unmapped address |
| `cpu.cpp` | HB-16: eight registers, counted cycles, halt and fault |
| `video.cpp` | The three modes, the DAC, the blitter and the sprites |
| `audio.cpp` | Four channels of 8-bit PCM and the waveform bank |
| `dos.cpp` | HB DOS 2.0: two drives, directories and a sector map |
| `shell.cpp` | The prompt's words |
| `hbos.cpp` | HB-OS: the scheduler, the tasks and the desktop |
| `monitor.cpp` | The monitor's registers and memory views |
| `asm.cpp` | The assembler the window, the tool and the tests share |
| `machine.cpp` | The object that ties them together |

`Hb2Window` ([`hb2_window.hpp`](../include/timewarp/ui/hb2_window.hpp)) shows it:
four pages — Screen, Prompt, Monitor, Program — and a 60 Hz timer that ticks the
machine and paints what the chip composed.

`hb2` ([`tools/hb2.cpp`](../tools/hb2.cpp)) is the same machine on a shell: it
assembles, runs a number of frames, prints the registers, the screen and a hash of
the framebuffer, and fails a program that ran and drew nothing.

## State

| What | Where |
| --- | --- |
| Preferences (both machines) | `QSettings` under `~/.config/HoneyBadger/HB Computers.conf` |
| HB DOS 1.0 volumes | `~/.time_warp/disks/` |
| Machine images | `~/.time_warp/` (`.hb1`) |
| HB-2 drives, tape and module bay | `~/.time_warp/hb2/` |
| Example programs | `Examples/`, found beside the build tree |

`TIME_WARP_DATA_DIR` moves everything under `~/.time_warp`, which is why a test can
run against a scratch folder and never touch a real machine.

## Tests

Every suite is an executable with a `main`, built on the two-hundred-line harness
in `tests/check.hpp`; there is no gtest and no package to install.

| Suite | Covers |
| --- | --- |
| `lexer_tests`, `evaluator_tests` | The expression engine |
| `scheme_tests` | The Scheme machine |
| `lang_tests` | Every executor, including the shipped examples |
| `audio_tests` | The mixer |
| `canvas_queue_tests`, `canvas_bridge_tests` | The queue and its Qt bridge |
| `syntax_highlighter_tests` | The editor's colouring |
| `hb2_tests` | The HB-2, the map, the processor, video, sound, DOS, HB-OS |
| `retro_computer_tests` | The HB-1: ROM socket, screen modes, direct mode, HB DOS, memory, boot, images, configuration |

`ctest --test-dir build` runs all of them. The two window suites need a display or
`QT_QPA_PLATFORM=offscreen`, which is what the tests set for themselves.
