# The Kestrel 16 Owner's Manual

HoneyBadger Systems' 1992 computer, and the Kestrel's successor. Same idea, ten
years on: a processor, a ROM socket, a tube, a keyboard and a disk, with the
language in ROM rather than in the host. What the decade bought is width — a
65816, a megabyte of banked memory, and sixteen-bit arithmetic in a language that
no longer shares a socket with the kernel.

```
KESTREL 16 BASIC 2.0
READY> PRINT 2+2
4

READY>
```

---

## The machine

| | |
| --- | --- |
| Processor | WDC 65C816 at 8 MHz, emulation and native modes |
| Frame | 50 Hz, so 160 000 cycles a frame |
| RAM | 1 MiB: bank zero and banks 8–15 |
| ROM | an 8 KiB KERNAL at the top of bank zero; a 64 KiB language ROM in bank one |
| Tube | 80 × 50 cells, sixteen colours for ink and sixteen for paper |
| Keyboard | a 32-key controller buffer |
| Sound | one latch byte, as on the Kestrel |
| Disk | a 2 MiB volume: 4096 sectors of 512 bytes |
| Cassette | a byte stream with a play head |

### The map

The 65816 sees twenty-four address lines, so the machine is a row of 64 KiB banks
rather than one page. Bank zero keeps the shape the Kestrel had.

| Bank | Address | What is there |
| --- | --- | --- |
| `$00` | `$0000`–`$00FF` | Zero page |
| `$00` | `$0100`–`$01FF` | The processor's stack |
| `$00` | `$0200`–`$07FF` | System RAM: the keyboard buffer, the line buffer, workspace |
| `$00` | `$0800`–`$7FFF` | Program, variables and free RAM (30 KiB) |
| `$00` | `$8000`–`$8F9F` | The tube: 4000 character cells, row-major |
| `$00` | `$8FA0`–`$9F3F` | One attribute byte per cell |
| `$00` | `$A000`–`$A0FF` | The I/O page |
| `$00` | `$A100`–`$BFFF` | Nothing responds: reads are zero, writes are lost |
| `$00` | `$C000`–`$DFFF` | Cartridge ROM, 8 KiB |
| `$00` | `$E000`–`$FFFF` | The KERNAL |
| `$01` | `$0000`–`$FFFF` | The language ROM |
| `$04`–`$07` | | A cartridge, 256 KiB |
| `$00`, `$08`–`$0F` | | RAM |

The map is written twice — `include/kestrel/model1992/map.hpp` for the emulator
and `roms/kestrel16/kestrel16.inc` for the ROMs — and the two are changed
together.

### The I/O page

| Offset | Register |
| --- | --- |
| `$00` | Keyboard data: the next key, `0` when there is none |
| `$01` | Keyboard status: bit 7 set while a key is waiting |
| `$02` | Write anything to clear the keyboard buffer |
| `$03` / `$04` | Cursor column / row |
| `$05` / `$06` / `$07` | Ink / paper / border |
| `$08` | The sound latch |
| `$09` / `$0A` | The frame counter, low byte first |
| `$0B` | Set every frame; write anything to clear it |
| `$0C` / `$0D` | Disk sector |
| `$0E` | Disk command: `1` read, `2` write, `3` format, `4` query |
| `$0F` | Disk status: busy, error, present, done |
| `$10`–`$12` | The disk's buffer address: a full 24-bit pointer, low byte first |
| `$13` / `$14` | How many sectors to move |
| `$15` | The noise generator |
| `$16` | The last key read |
| `$17` | Set when the screen scrolled |
| `$18` | Cursor on |
| `$19` | Tape data |
| `$1A` | Tape status |
| `$1B` | Tape control: play, record, stop, rewind |

A write to a register is an output to a device and a read is an input from one.

---

## Switching on

The 65816 leaves reset in emulation mode, as the chip does, and the KERNAL's
first few instructions leave it: `CLC` / `XCE`, a sixteen-bit stack, a direct page
at zero and a data bank at zero. From there it clears the screen, prints its
banner and starts the language in bank one with `JML`.

`BREAK` is a warm start: a non-maskable interrupt, a newline and the language
again.

---

## The KERNAL

The KERNAL knows nothing about any language. Its services are fixed addresses in
a table at `$00:E000`, which is what a caller in bank zero uses:

| Address | Service |
| --- | --- |
| `$E000` | `CHROUT` — A = character |
| `$E003` | `GETIN` — A = the next key, or `0` |
| `$E006` | `CHRIN` — the next key, waiting for one |
| `$E009` | `CLS` |
| `$E00C` | `HOME` |
| `$E00F` | `PRINT` — `$10` points at the text, X = its length |
| `$E012` | `SCROLL` |
| `$E015` | `LOCATE` — X = column, Y = row |
| `$E018` | `GETLINE` — read into the buffer at `$10`, length in X |
| `$E01B` | `READSECT` — X = sector, buffer at `$10` |
| `$E01E` | `WRITESECT` |
| `$E021` | `CURSORON` |
| `$E024`–`$E036` | The Kestrel DOS 2.0 entry points |
| `$E039` | `SETINK` |
| `$E03C` | `SETPAPER` |
| `$E03F` | `SOUND` |
| `$E042` | `FRAMES` — the frame counter |
| `$E045` | `MEMTOP` — X = the top of free RAM |
| `$E048` | `PEEKBYTE` — A = bank, X = address |
| `$E04B` | `POKEBYTE` — A = bank, X = address, Y = value |
| `$E04E` | `GETKEY` |
| `$E051` | `NEWLINE` |

A `JSR` stays in the program bank on this processor, so the language in bank one
cannot use that table. Beside it, at `$00:FF00`, is the same list again — four
bytes an entry, `JSR` and `RTL` — and a caller in another bank reaches it with
`JSL`. Both tables exist because the processor makes them necessary.

### The frame

Fifty times a second the machine raises its interrupt. The handler counts frames,
puts the cursor where the tube should draw it, blinks it every thirty-two frames,
and empties the keyboard controller into the KERNAL's buffer.

---

## Kestrel 16 BASIC 2.0

A line-oriented BASIC with sixteen-bit integers, in the tradition the decade
inherited: type a line, press Return, read the answer.

```
READY> PRINT 2+2
4
> LET A=41
> PRINT "THE ANSWER IS ";A+1
THE ANSWER IS 42
```

### What the language does today

| Statement | |
| --- | --- |
| `PRINT` | Expressions and strings, separated by `;` (no space) and `,` (a space). A trailing `;` leaves the line open. `?` is shorthand |
| `LET` | `LET A=41`, or `A=41`; variables are `A` to `Z`, sixteen bits each |
| `CLS` | Clear the screen and print the banner |
| `REM` | A remark |
| `END` | Finish the line |
| `NEW` | Start again |

Expressions are `+ - * /` with parentheses and unary minus; `*` and `/` bind
tighter than `+` and `-`. Values are sixteen-bit signed integers, so
`PRINT 30000+1` is `30001` and `PRINT 1000*20` is `20000` — neither of which the
Kestrel's BASIC could hold.

Anything else is a mistake, and the machine says so:

```
READY> WIBBLE
?WHAT?
```

### What it does not do yet

The honest list, because a manual that hides it is worse than useless:

- **no stored program** — there are no line numbers, so no `RUN`, `LIST`, `GOTO`,
  `GOSUB`, `FOR`/`NEXT` or `IF`. The line reader and the statement dispatcher are
  built for them: a line number in front of a statement is the next thing to go
  in;
- **no strings as values** — `PRINT "text"` works, `A$` does not;
- **no arrays, no functions** — no `DIM`, no `ABS` or `LEN`, no `DEF FN`;
- **no filesystem** — the disk device moves sectors, and Kestrel DOS 2.0's jump
  table entries answer `1` (an error) until it is written;
- **no cassette blocks** — the tape is a byte stream and the KERNAL has the
  registers, but the block format is not written.

---

## Writing for the machine

The interpreter is `roms/kestrel16/basic.s`; the KERNAL is
`roms/kestrel16/kernal.s`; the map and the services are
`roms/kestrel16/kestrel16.inc`. All three are assembled by `kestrel16_asm`, and
the build does it:

```bash
cmake --build build --target kestrel16_roms
```

### Two rules the ROMs follow

**The accumulator's width is a convention, not a guess.** `SEP #$20` makes the
accumulator a byte and `REP #$20` makes it a word; every immediate between a
`SEP` and its matching `REP` says `.B` or nothing, and every word immediate says
`.W`. The assembler cannot see the flags, so the source says what it means, and
an immediate that does not fit its width is an error rather than a silent
truncation.

**The tube's position is the row and column pair.** `ZP_ROW` and `ZP_COL` are the
only copy and `CELLOFFSET` turns them into an address. An offset kept alongside
them drifts, and the drift shows up as text in the wrong place.

[The tools page](../TOOLS.md) covers the dialect in full.

---

## Colophon

The machine is `kestrel::Machine` built for `Model::Kestrel16`. Its tests type at
it and read the screen back: `kestrel16_cpu_tests` for the processor,
`kestrel16_machine_tests` for the machine and its ROMs, `kestrel16_asm_tests` for
the dialect. The 1982 machine is [the Kestrel](../kestrel/README.md).
