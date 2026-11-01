# The machines

This repository is two computers and the engines they run on. Neither is a
screenshot, a theme, or a video of a computer: they are places you can type at,
break and come back to.

- the **[HB-1](hb1/README.md)** — a personal computer from a 1982 that never
  happened, shipped with eleven language ROMs in one socket, a prompt, a CRT and a
  disk; and
- the **[HB-2](hb2/README.md)** — the HB-1's 1992 successor, built from
  [the Model 92 proposal](hb1/model-92.md): a bus, a 640 × 480 framebuffer and a
  blitter, four channels of sampled audio, hardware sprites, and HB-16, a processor
  of the machine's own, with a desktop, a disk of its own and a prompt.

Both are real programs. The HB-1 runs the native engines in process; the HB-2 runs
nine of them on its own bus as well as an instruction set of its own, and it is a
machine you can disassemble.

---

## A system from a parallel universe

The HB-1 is not an emulator of a machine that existed. It is what a 1982 home
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
| `*** HB-1 ***` and `(C) 1982 HB SYSTEMS` | The same native engines in process, no Python | The banner sets the stakes; the engine removes the crashes |
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

| | HB-1 | HB-2 |
| --- | --- | --- |
| What it is | A complete home computer: CRT, prompt, ROM socket, editor, canvas, disk | A 1992 workstation: a bus, a framebuffer, four channels of sampled audio, a desktop |
| Year, in its universe | 1982 | 1992 |
| Screen | TEXT / SPLIT / GRAPHICS sharing one CRT | 80×25 text, 320×200×256 and 640×480×16, with the 1982 map as `MODE 0` |
| Languages | All eleven, one ROM at a time | HB-16 machine code, plus nine of the engines on the ROM socket |
| Storage | HB DOS 1.0, 512 sectors, one volume | HB DOS 2.0 with two drives, directories and a sector map, and a cassette |
| Interaction | A prompt with history, BASIC direct mode, F9 to run | A prompt, a monitor, a screen and a listing page; eight processes on a desktop |
| Where it lives | `hb_computers --hb1`, or the launcher | `hb_computers --hb2`, or the launcher |
| Manual | [HB-1 Owner's Manual](hb1/README.md) | [HB-2 Manual](hb2/README.md) and [HB-16](hb2/instruction-set.md) |
| Teaches | That a computer is a system: memory, storage, languages, a screen | That a system is a set of programs: a bus, a processor, an operating system and a desk to put them on |

They are deliberately different kinds of thing. The HB-1 is a place to live; the
HB-2 is what the same company shipped ten years later, and what that decade asked
of it. Together they bracket the era: the home computer that made a computer out
of a chip, and the workstation that made it run several programs at once.

---

## What they share

Neither machine is a special case inside the application:

- the HB-1 draws through the `CanvasCommandQueue` into the turtle canvas, and the
  HB-2 composes its own framebuffer from the same kind of command the machine's
  `INT` services leave behind;
- both run programs on the same native engines — `hb_lang` for the line-by-line
  languages, `hb_scheme` for Scheme, and the CHIP-8 assembler and machine that the
  HB-1's CHIP-8 ROM uses. The HB-2's ROM socket puts nine of those engines on the
  machine's own bus through `Interpreter::MachineBus`, so a language program's
  `POKE` reaches the machine rather than a map beside it;
- the two machines use `NativeRunner` for a run that finishes and `ReplRunner` for
  a session that does not, which is why a program can never start inside a previous
  command's variables;
- both are driven, in tests, through the same public surface a user touches:
  `submitCommand()`, `runProgram()`, `disk()` — and, on the HB-2, `runRom()`,
  `spawn()` and `scheduler()`.

Both are native and neither involves a second interpreter. How the pieces are put
together is [ARCHITECTURE.md](ARCHITECTURE.md).

---

## The manuals

### The HB-1

| Book | What it covers |
| --- | --- |
| [Owner's Manual](hb1/README.md) | The front panel, first boot, everyday operation, what the machine remembers, the limits |
| [The ROM library](hb1/roms.md) | All eleven languages as ROMs: banner, prompt, direct mode, session, worked examples |
| [HB DOS 1.0](hb1/hb-dos.md) | The disk: every word, the catalogue, the errors, the image format |
| [CONFIG](hb1/config.md) | Every setting, every default, the phosphors and the glass, where it is stored |
| [Inside the HB-1](hb1/inside.md) | How a keystroke becomes ink: the objects behind the screen |
| [The Model 92 proposal](hb1/model-92.md) | The 1992 machine, proposed before it was built |
| [Enhancements](hb1/enhancements.md) | What the machine could grow next |

### The HB-2

| Book | What it covers |
| --- | --- |
| [Owner's Manual](hb2/README.md) | The bus, the map, the video chip, the sound, the shell, the desktop, the limits |
| [The instruction set](hb2/instruction-set.md) | Every HB-16 opcode as implemented, and the encoding the assembler and disassembler share |

### Around them

- [ARCHITECTURE.md](ARCHITECTURE.md) — how the pieces are put together, for
  people changing them.
- [../README.md](../README.md) — building and running, and the layout of the tree.
