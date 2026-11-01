# The Kestrel Owner's Manual

The Kestrel has a processor, and the processor runs ROMs.

This is the machine's manual: what the hardware is, how the ROMs are made, what
is in them, and exactly how far they have got.

---

## What the machine is

| | |
| --- | --- |
| Processor | **MOS 6502 at 1 MHz**, cycle-counted, in `src/kestrel/model1982/cpu6502.cpp` |
| Memory | 64 KiB address space, 32 KiB of it RAM |
| Tube | 40 × 25 characters, one ASCII code per cell, plus an ink/paper byte per cell |
| Keyboard | a 16-key controller buffer, drained by the frame interrupt |
| Sound | one latch byte, played by the host |
| Disk | 512 sectors of 256 bytes, a sector-level controller and Kestrel DOS in ROM |
| ROM | 16 KiB socket at `$A000` for the language, 8 KiB KERNAL at `$E000` |

Nothing on that list is a picture of a feature. The screen is memory the
processor writes; the disk is a sector of bytes the processor moves; the
`READY.` prompt is a string in a ROM.

### The map

```text
$0000-$00FF   zero page            KERNAL owns $00-$1F, the socket ROM $20-$FF
$0100-$01FF   the processor's stack
$0200-$02FF   the input line
$0300-$06FF   Kestrel DOS buffers (directory, volume header, one data sector)
$0700-$070F   the keyboard buffer the interrupt handler fills
$0801-$7EFF   program, variables and free memory
$7F00-$7FFF   the interpreter's stacks (values, FOR loops, GOSUB frames)
$8000-$83E7   the screen, 40 x 25
$8400-$87E7   colour, one byte per cell: ink << 4 | paper
$8800-$88FF   I/O registers
$8900-$9FFF   nothing responds
$A000-$DFFF   the ROM in the socket
$E000-$FFFF   the KERNAL, with the three vectors in the top six bytes
```

The I/O registers are listed in
[`map.hpp`](../../include/kestrel/model1982/map.hpp) and again in
[`kestrel.inc`](../../roms/kestrel/kestrel.inc), deliberately: the emulator decodes the
addresses and the ROM pokes them, and a test compares the pair by writing the
registers the ROM writes.

---

## Switching it on

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/kestrel_window
```

`kestrel_window` is the machine on its own: the window is the tube, the keyboard
is yours, and there is no interpreter behind it. `--shot <file>` boots the ROM,
lets the banner finish, and writes the screen to a PNG, which is how the window
is checked without a person watching it.

The keys are the machine's: `Return`, `Backspace` (Rub Out), `Escape`, and
`Ctrl+Q` to leave the machine on. Everything printable goes to the keyboard
controller, sixteen keys at a time.

## The front panel

Everything a 1982 owner could change by reaching round the back is a menu, and
the choices are remembered between sessions (`QSettings`).

| Menu | What it changes |
| --- | --- |
| **Machine → Speed** | 1 MHz (the machine's own clock), 2 MHz, 4 MHz, or "as fast as this computer" -- the 6502 is the same processor; this is how many of its cycles the host gives it per second |
| **Machine → ROM socket** | which 16 KiB image is at `$A000`; changing it cold-boots the machine into that ROM |
| **Machine → Cartridge** | fits a 4 KiB image at `$9000`, read-only to the processor, or ejects it |
| **Machine → Sound** | mutes the latch; the ROM keeps writing to it and the counter keeps counting |
| **Media → Mount/Save/Format disk** | a `.dsk` is the raw volume: 512 sectors of 256 bytes, 128 KiB |
| **Media → Insert/Save/Eject tape** | a `.tap` is the byte stream the cassette device serves |

The machine API behind the menus is small and has no Qt in it: `setSpeed`,
`mountSocketRom`, `mountCartridge`, `ejectCartridge`, `setSoundEnabled`,
`disk().load/save/format`, `tape().load/insert/eject`. The tests drive those
directly, which is why the menus are not the only place the features work.

### The cassette

A 1982 tape was one bit and a motor. The device here is the part under the pulse
encoding: a byte stream with `play`, `record`, `stop` and `rewind`, and a status
byte (`$8819`: present, playing, recording, data left). The KERNAL does not yet
speak a tape format; the **monitor ROM** does -- `S aaaa bbbb` writes a
four-byte header and the block, `L` reads it back -- so the hardware is proved
end to end (BASIC keeps to the disk and leaves tape to the monitor).

## The ROM library

`romLibrary()` in [`roms.cpp`](../../src/kestrel/model1982/roms.cpp) is the table of every
image this build carries. Each is a 16 KiB socket ROM or a 4 KiB cartridge, and
nothing in the emulator knows what any of them do.

| id | ROM | State |
| --- | --- | --- |
| `basic` | BASIC V2.0 | **complete for what it is**: the statement set, the function library and the disk commands described below |
| `monitor` | MONITOR V1.0 | **complete for what it is**: `M` examine, `:` deposit, `G` go, `R` registers, `S` save block to tape, `L` load block from tape |

The monitor is the second ROM in the series and the proof that the socket is
real: it is assembled from [`roms/kestrel/monitor.s`](../../roms/kestrel/monitor.s),
mounted at run time, and boots into its own banner without the KERNAL changing.

---

## The ROMs are built, not compiled in

`roms/kestrel/*.s` is 6502 assembly. `tools/kestrel_asm.cpp` assembles it at build time
into the images the machine boots:

```text
cmake --build build --target kestrel_roms
build/generated/kestrel/basic_rom.hpp     16 KiB at $A000, as a constexpr array
build/generated/kestrel/kernal_rom.hpp     8 KiB at $E000
build/generated/kestrel/basic.sym         every label and its address
build/generated/kestrel/basic.lst         the listing, instruction by instruction
```

The assembler is a build tool, not a dependency: the tree still builds with a
C++20 compiler and nothing else. It iterates to a fixed point rather than
guessing in two passes, so a branch that cannot reach its target becomes its own
inverse branch over a `JMP` and a label that lands in zero page is assembled as a
zero-page instruction without being told to. The images are byte-for-byte
reproducible, and `.sy`/`.lst` are how a wrong byte is found.

---

## What is in the KERNAL (8 KiB)

* **Reset** -- hardware initialisation, `CLI`, then `JMP $A000`, the cold start
  of whatever ROM is in the socket.
* **The frame interrupt** -- counts 50 Hz frames, blinks the cursor, and empties
  the keyboard controller into the KERNAL's own sixteen-place buffer. It has to:
  the controller's interrupt line stays high until its last key is read.
* **The screen** -- `CHROUT` with `CR`, Rub Out, `CLS`, `HOME` and colour codes;
  wrap at column 40; a scroll that moves the matrix and the colour map and tells
  a program it happened.
* **The keyboard** -- `GETIN`, `CHRIN`, and `GETLINE` with Rub Out as its only
  editor. A 1982 machine at this price was a teletype: you typed the line, and if
  you wanted a different one you typed it again.
* **The disk controller** -- read a sector into RAM, write RAM onto a sector.
* **Kestrel DOS 1.0** -- the volume header, a 64-byte free map, two directory
  sectors (32 files of 8.3-ish names), 252-byte chains, `CATALOG`, `FIND`,
  `LOAD`, `SAVE`, `DELETE`, `FORMAT`.
* **Number output** -- decimal conversion, for the directory listing.

Every entry point is a fixed address in the jump table at `$E000`
(`K_CHROUT`, `K_GETLINE`, `K_DOSSAVE`, …), which is how the socket ROM calls the
KERNAL, and how a machine-code program a person writes can too.

## What is in the BASIC ROM (16 KiB)

* The banner, `READY.`, the prompt loop, and a `HELP` command that lists the
  language the ROM actually carries.
* **Arithmetic**: 5-byte floating point in the MS layout -- exponent, then a
  four-byte mantissa whose top bit is the sign -- with add, subtract, multiply,
  divide, compare, integer conversion, and decimal input and output (nine
  significant digits, scientific notation outside 1e-4..1e9).
* **Functions**: `ABS`, `INT`, `SGN`, `SQR`, `RND`, `PEEK`, and the
  transcendentals `EXP`, `LOG`, `SIN`, `COS`, `TAN`, `ATN`, the last four by
  series after range reduction, accurate to about nine digits.
* **Expressions**: `OR`, `AND`, `NOT`, comparisons (`=`, `<>`, `<`, `>`, `<=`,
  `>=`), `+ - * /`, `^` (an integer power is multiplied out, so `2^10` is
  exactly 1024), unary sign, brackets, numbers in decimal and `E` notation.
* **Variables**: one or two characters, numeric and `$` string names, looked up
  and created as they are met.
* **Functions of your own**: `DEF FN A(X)=X*X+1`, then `FN A(3)`. The body is
  kept where it was written in the program, so a definition typed at the prompt
  is worth nothing -- the text it points at is gone by the next line. Four
  definitions at a time, numeric only.
* **Strings**: string variables with a heap that grows down from the top of
  memory (no collector -- what is written is not given back), assignment, use in
  `PRINT`, `+` to join two of them, `= <> < > <= >=` to compare them (shorter
  sorts first), and `LEN`, `ASC`, `CHR$`, `STR$`, `VAL`, `LEFT$`, `RIGHT$`,
  `MID$`. A string where a number belongs, or the other way round, is
  `?TYPE MISMATCH`.
* **Arrays**: `DIM A(10)`, `DIM M(2,3)`, `DIM A$(10)` -- up to four dimensions,
  of numbers or of strings, elements of
  five bytes each, the last subscript moving fastest, allocated from the same
  heap as the strings and kept for the life of the program. A name used with
  one subscript before it is dimensioned becomes an array of eleven, as it did
  on the machines of the day; `?BAD SUBSCRIPT` and `?REDIMENSIONED ARRAY` say
  the rest.
* **Program storage**: numbered lines stored in number order with linked
  headers, `LIST`, `LIST n`, `RUN` from the start, `NEW`, `DELETE n[,m]`, and
  multi-statement lines with `:`.
* **Statements**: `PRINT` (with `?`, `;` and `,`), `LET` (with or without the
  keyword), `IF`/`THEN`/`ELSE`, `FOR`/`TO`/`STEP`/`NEXT`, `GOTO`, `GOSUB`,
  `RETURN`, `ON`…`GOTO`/`GOSUB`, `INPUT`, `GET` (one key, no Return),
  `DATA`/`READ`/`RESTORE` (numbers and quoted strings alike), `REM`, `END`,
  `STOP` with `CONT` to carry on from where it stopped, `POKE`, `CLS`.
* **Disk**: `SAVE "NAME"`, `LOAD "NAME"`, `CATALOG`, all through Kestrel DOS, which
  keeps the name, the first sector and the length in the directory.
* **Errors** as the machine reports them: `?SYNTAX ERROR`,
  `?DIVISION BY ZERO ERROR`, `?OUT OF MEMORY`, `?OUT OF DATA`, `?BAD DATA`,
  `?ILLEGAL QUANTITY`, `?TYPE MISMATCH`, `?STRING TOO LONG`, `?BAD SUBSCRIPT`,
  `?REDIMENSIONED ARRAY`, `?UNDEF'D STATEMENT`,
  `?RETURN WITHOUT GOSUB`,
  `?NEXT WITHOUT FOR`, `?BAD FILE NAME`, `?DISK ERROR`, `?OVERFLOW`, with the
  line number attached when a program is running.

## Where the boundary is

This is a working machine. Being exact about the edge is the difference between
a ROM and a demonstration:

**Working and tested** (see `tests/kestrel_machine_tests.cpp`, which types at the
machine and reads the screen): everything listed above, plus the screen as
memory, sector-level disk access, the frame clock and the sound latch.

**Not in this ROM yet:** tape `CSAVE`/`CLOAD` from BASIC. The tape hardware is
proven end to end by the monitor's `S` and `L`, which write and read the block
format `[address][count][bytes]`; a `CLOAD` for BASIC was written and removed
again because a misread header could copy over the screen, and a command that
can do that is worse than one that is missing. `HELP` says so, on the machine.

**Not started:** the other ten language ROMs (Logo, PILOT, FOCAL, COMAL, REXX,
APL, Smalltalk, Forth, Scheme, CHIP-8). The socket is designed for them: each is
a 16 KiB image at `$A000` whose first three bytes are the cold start and whose
next three are the warm start, and the KERNAL needs no changes.

---

## Adding to the ROM

1. Write it in `roms/kestrel/*.s`, in the dialect `tools/kestrel_asm.cpp` documents at the
   top of that file.
2. Call the KERNAL through the `K_` addresses in `kestrel.inc`, never by reaching
   into its internals.
3. `cmake --build build --target kestrel_roms` assembles it; `basic.lst` shows what
   came out and `basic.sym` says where every label landed.
4. Prove it in `tests/kestrel_machine_tests.cpp` by typing at the machine and
   reading the screen -- never by calling into the ROM from C++.

The CPU has its own suite (`tests/kestrel_cpu_tests.cpp`) covering addressing modes,
BCD arithmetic, the stack, the three interrupts, the `JMP ($xxFF)` bug and the
cycle counts the 20 000-cycle frame depends on.
