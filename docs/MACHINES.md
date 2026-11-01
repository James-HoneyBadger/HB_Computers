# The machines

This repository is two computers and the engines they run on. Neither is a
screenshot, a theme, or a video of a computer: they are places you can type at,
break and come back to.

- the **[Kestrel](kestrel/README.md)** — a personal computer from a 1982 that never
  happened, shipped with eleven language ROMs in one socket, a prompt, a CRT and a
  disk; and
- the **[Kestrel 16](kestrel16/README.md)** — the Kestrel's 1992 successor: a 65816 at 8 MHz, a
  megabyte of banked memory, an 80 x 50 tube, and KESTREL 16 BASIC 2.0 in a sixty-four
  kilobyte bank of its own.

Both are real programs, and they are **one program**: `timewarp::Machine` is built
for a model, and a profile table says what that model is — its clock, its memory,
where its screen and its registers are, how big its ROMs are and how its disk is
divided. `kestrel --model kestrel` and `kestrel --model kestrel16` are the same
executable, and the two test suites boot both models through the same class.

They are also the same kind of thing: the host supplies a processor, memory, a
tube, a keyboard and a disk, and the languages are machine code assembled into a
ROM image and executed. Neither machine has an interpreter written for it in C++.

---

## A system from a parallel universe

The Kestrel is not an emulator of a machine that existed. It is what a 1982 home
computer might have been if the people who designed it had also known everything
the next four decades taught us about making computers kind to learn on.

That single premise decides everything about it:

- **One ROM socket, eleven languages.** The parallel 1982 did not have a format war
  over BASIC. Its machine shipped with BASIC, Logo, PILOT, FOCAL, COMAL, REXX,
  Scheme, APL, Smalltalk, CHIP-8 and Forth, each a cartridge you select from the
  front panel, each keeping its own memory when you switch away.
- **A disk in the old measurements.** The volume is 512 sectors of 256 bytes,
  named files with a lock bit, a catalogue with a `*` beside the protected ones,
  and a two-step `INIT` — because that is how a 1982 learner learned that a disk
  is a physical thing you can destroy. Underneath, the image is one JSON file you
  can copy, read and hand to somebody else.
- **A prompt with modern manners.** The prompt echoes in the machine's voice and
  refuses in the machine's voice (`?FILE LOCKED`), but it is a persistent session
  with real bindings, and the text above it on the PROGRAM page is a full code
  editor with undo, syntax colouring and a gutter.
- **A monitor you can tune.** Green, amber, white or ice phosphor, scanlines,
  vignette, curvature, flicker and glow — each a slider from nothing to
  everything. The glass is drawn over the screen and never takes the keyboard.
- **Nothing you can lose by exploring.** CONFIG previews every change before you
  accept it and Cancel puts the machine back. Breakpoints, the Variables panel,
  watch expressions and telemetry work behind period controls. The worst thing a
  program can do is fill 512 sectors and stop.

The result is a machine that feels inevitable and impossible at the same time:
the constraints are 1982's, the engines are real, and the only thing missing is
the cruelty.

---

## The timey-wimey, wibbly-wobbly parts

Anachronism on purpose, item by item. The left column is what a 1982 learner
would recognise; the right is what is actually running.

| Period thing | What is really there | Why it is there |
| --- | --- | --- |
| `*** Kestrel ***` and `(C) 1982 HONEYBADGER SYSTEMS` | The same native engines in process, no Python | The banner sets the stakes; the engine removes the crashes |
| 512 sectors of 256 bytes | One JSON image under `~/.time_warp/disks` | Sector accounting teaches that storage is finite; JSON means a teacher can copy the disk |
| `?` for `PRINT`; numbered lines | A real code editor on the PROGRAM page | Classic direct mode and modern editing are the same program seen two ways |
| A catalogue with a lock bit | `HbDos`, a widget-free class with rollback on a failed write | The period gave the metaphor; the class makes it testable |
| A CRT with phosphor and glass | `retroPalette` plus `CrtOverlay`, applied to the whole machine | The fantasy is total: choosing amber recolours header, editor, canvas and glass together |
| A settings screen you reach with `CONFIG` | A live-preview dialog that persists under `retroComputer/config` | The ritual is period; the preview and Cancel are courtesy |
| A prompt | A persistent REPL with bindings in eight of the eleven ROMs | You can learn interactively, as the 1980s promised, and keep what you defined |

The machines also disagree with history where history was simply wrong. For
example, `SYS` is a no-op: `0NNN` called a routine on the RCA 1802 that hosted the
original CHIP-8 interpreter, nothing here can run it, so the machine ignores it —
as every interpreter since the original has — and the assembler still accepts the
mnemonic so old listings assemble.

---

## The machines at a glance

| | Kestrel | Kestrel 16 |
| --- | --- | --- |
| What it is | A complete home computer: a 6502, a 64K, a tube, a disk, ROMs in a socket | The same machine ten years on: a 65816, a megabyte, banked ROMs |
| Year, in its universe | 1982 | 1992 |
| Processor | MOS 6502 at 1 MHz | WDC 65C816 at 8 MHz, native mode |
| Screen | 40 x 25 cells with a colour byte each | 80 x 50 cells, sixteen colours for ink and paper |
| Languages | Eleven, one ROM at a time, in a 16 KiB socket | KESTREL 16 BASIC 2.0 in a bank of its own, with the KERNAL beside it |
| Numbers | Five-byte floating point, eight-bit integers | Sixteen-bit integers |
| Storage | Kestrel DOS 1.0, 512 sectors of 256 bytes | A 2 MiB volume in 4096 sectors of 512 bytes; Kestrel DOS 2.0 is not written yet |
| Where it lives | `kestrel_computers --hb1`, or the launcher | `kestrel16_machine_tests`, and `kestrel16_asm` for its ROMs |
| Manual | [Kestrel Owner's Manual](kestrel/README.md) | [Kestrel 16 Manual](kestrel16/README.md) |
| Teaches | That a computer is a chip, a map and a program in ROM | That a decade buys width: sixteen-bit numbers, banked memory, and a bigger language |

They are deliberately the same kind of thing, ten years apart. Both are machines
whose languages are machine code in ROM, executed by a processor the host
supplies; neither is an interpreter written twice. What separates them is what the
decade actually changed: a wider processor, memory in banks, and a language with
sixteen-bit numbers in a ROM big enough to hold one.

---

## What they share

Neither machine is a special case inside the application:

- both decode an address map in one place for the emulator and repeat it in the
  ROMs' include file, so a register cannot be at one address for the machine and
  another for the code;
- both prove themselves by typing at the machine rather than by calling a ROM
  routine from C++: a keystroke goes in, frames run, and the screen is read back;
- both assemble their ROMs at build time, from the same shape of source, with the
  same kind of listing and symbol file;
- both run at fifty frames a second, and both count a program's idea of time in
  processor cycles.

The Kestrel 16 is the Kestrel's design carried forward rather than a second design. How the
pieces are put together is [ARCHITECTURE.md](ARCHITECTURE.md).

---

## The manuals

### The Kestrel

| Book | What it covers |
| --- | --- |
| [Owner's Manual](kestrel/README.md) | The front panel, first boot, everyday operation, what the machine remembers, the limits |
| [The ROM library](kestrel/roms.md) | All eleven languages as ROMs: banner, prompt, direct mode, session, worked examples |
| [Kestrel DOS 1.0](kestrel/hb-dos.md) | The disk: every word, the catalogue, the errors, the image format |
| [CONFIG](kestrel/config.md) | Every setting, every default, the phosphors and the glass, where it is stored |
| [Inside the Kestrel](kestrel/inside.md) | How a keystroke becomes ink: the objects behind the screen |
| [The Model 92 proposal](kestrel/model-92.md) | The 1992 machine, proposed before it was built |
| [Enhancements](kestrel/enhancements.md) | What the machine could grow next |

### The Kestrel 16

| Book | What it covers |
| --- | --- |
| [Owner's Manual](kestrel16/README.md) | The map, the I/O page, the KERNAL's services, the language, the dialect, the limits |

### Around them

- [ARCHITECTURE.md](ARCHITECTURE.md) — how the pieces are put together, for
  people changing them.
- [../README.md](../README.md) — building and running, and the layout of the tree.
