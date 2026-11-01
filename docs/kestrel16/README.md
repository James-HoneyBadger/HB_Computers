# The Kestrel 16 Owner's Manual

A 1992 home computer from the same company that made the [Kestrel](../kestrel/README.md),
and the same kind of machine: a processor, a ROM socket, a tube, a keyboard and a
disk, with the languages in ROM rather than in the host.

Ten years bought three things the Kestrel could not have, and this machine is built
out of exactly those: a **65816** with a sixteen-megabyte address space, **banked
ROMs** so the language no longer shares a socket with the kernel, and **sixteen-bit
arithmetic** as the natural width of the language.

Nothing on the screen is drawn by the host. The host supplies a processor, memory,
a tube, a keyboard and a disk; `KESTREL 16 BASIC 2.0` and its answers are machine code
assembled into `roms/kestrel16/` and executed at 8 MHz.

---

## The machine

| | |
| --- | --- |
| Processor | WDC 65C816 at 8 MHz, emulation and native modes |
| Frame | 50 Hz, so 160 000 cycles a frame |
| RAM | 1 MiB: bank zero and banks 8 to 15 |
| ROM | 8 KiB KERNAL at the top of bank zero; 64 KiB language ROM in bank one |
| Screen | 80 x 50 character cells, one of sixteen colours for ink and paper each |
| Storage | A 2 MiB volume: 4096 sectors of 512 bytes |
| Sound | One latch byte, as on the Kestrel |
| Cassette | A byte stream with a play head |

### The address map

| Bank | Address | What is there |
| --- | --- | --- |
| `$00` | `$0000-$00FF` | Zero page |
| `$00` | `$0100-$01FF` | The processor's stack |
| `$00` | `$0200-$07FF` | System RAM: the keyboard buffer, the line buffer, workspace |
| `$00` | `$0800-$7FFF` | Program, variables and free RAM (30 KiB) |
| `$00` | `$8000-$8F9F` | Screen characters, 80 x 50, row-major |
| `$00` | `$8FA0-$9F3F` | Attributes, one (ink, paper) byte per cell |
| `$00` | `$A000-$A0FF` | I/O registers |
| `$00` | `$A100-$BFFF` | Nothing responds: reads are zero, writes are lost |
| `$00` | `$C000-$DFFF` | Cartridge ROM, 8 KiB |
| `$00` | `$E000-$FFFF` | KERNAL ROM |
| `$01` | `$0000-$FFFF` | The language ROM |
| `$04-$07` | | A cartridge, 256 KiB |
| `$00`, `$08-$0F` | | RAM |

The map is written twice — once in `include/kestrel/model1992/map.hpp` for the
emulator and once in `roms/kestrel16/kestrel16.inc` for the ROMs — and the two are changed
together.

### The I/O page

| Offset | Register |
| --- | --- |
| `$00` | Keyboard data: the next key, zero when there is none |
| `$01` | Keyboard status: bit 7 set while a key is waiting |
| `$02` | Write anything to clear the keyboard buffer |
| `$03` / `$04` | Cursor column / row |
| `$05` / `$06` / `$07` | Ink / paper / border |
| `$08` | The sound latch |
| `$09` / `$0A` | The frame counter, low byte first |
| `$0B` | Set every frame; write anything to clear it |
| `$0C` / `$0D` | Disk sector |
| `$0E` | Disk command: 1 read, 2 write, 3 format, 4 query |
| `$0F` | Disk status: busy, error, present, done |
| `$10-$12` | Disk address: a full 24-bit pointer, low byte first |
| `$13` / `$14` | Disk sector count |
| `$15` | The noise generator |
| `$16` | The last key read |
| `$17` | Set when the screen scrolled |
| `$18` | Cursor on |
| `$19` | Tape data |
| `$1A` | Tape status |
| `$1B` | Tape control: play, record, stop, rewind |

A write to a register is an output to a device and a read is an input from one;
the page is not storage.

---

## Switching on

The processor leaves reset in emulation mode, as the chip does, and the KERNAL's
first dozen instructions leave it: `CLC` / `XCE`, a sixteen-bit stack, a direct
page at zero, and a data bank at zero. From there the KERNAL clears the screen,
prints its banner and starts the language in bank one with `JML`.

```
Kestrel 16  1992                      <- the KERNAL
KESTREL 16 BASIC 2.0     <- the language
READY
> _
```

`BREAK` is a warm start: a non-maskable interrupt, a newline and the language
again.

---

## The KERNAL

The KERNAL is the part of the machine that is always there, and it knows nothing
about any language. Its services are reached through a jump table at `$00:E000`,
which is what a caller in bank zero uses:

| Address | Service |
| --- | --- |
| `$E000` | `CHROUT`: A = character |
| `$E003` | `GETIN`: A = the next key, or zero |
| `$E006` | `CHRIN`: the next key, waiting for one |
| `$E009` | `CLS` |
| `$E00C` | `HOME` |
| `$E00F` | `PRINT`: `$10` points at the text, X = its length |
| `$E012` | `SCROLL` |
| `$E015` | `LOCATE`: X = column, Y = row |
| `$E018` | `GETLINE`: read a line into the buffer at `$10`, X = its length |
| `$E01B` | `READSECT`: X = sector, `$10` = where it goes |
| `$E01E` | `WRITESECT` |
| `$E021` | `CURSORON` |
| `$E024`-$E036 | The Kestrel DOS 2.0 entry points |
| `$E039` | `SETINK` |
| `$E03C` | `SETPAPER` |
| `$E03F` | `SOUND` |
| `$E042` | `FRAMES`: the frame counter |
| `$E045` | `MEMTOP`: X = the top of free RAM |
| `$E048` | `PEEKBYTE`: A = bank, X = address |
| `$E04B` | `POKEBYTE`: A = bank, X = address, Y = value |
| `$E04E` | `GETKEY` |
| `$E051` | `NEWLINE` |

A `JSR` stays in the program bank on this processor, so the language in bank one
cannot use that table. Beside it, at `$00:FF00`, is the same list again — four
bytes an entry, `JSR` and `RTL` — and a caller in another bank reaches it with
`JSL`. The two tables exist because the processor makes them necessary, not
because either is redundant.

### The frame

Fifty times a second the machine raises its interrupt. The handler counts frames,
moves the cursor's position to the register the tube reads, blinks it every
thirty-two frames, and empties the keyboard controller into the KERNAL's
thirty-two-byte buffer — which it must do, because the controller's interrupt
line stays high until its last key is read.

---

## KESTREL 16 BASIC 2.0

A line-oriented BASIC with sixteen-bit integers, in the tradition the decade
inherited: type a line, press Return, read the answer.

```
READY
> PRINT 2+2
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

Expressions are the four arithmetic operators with `+ - * /`, parentheses and
unary minus, evaluated left to right with the usual precedence: `*` and `/` bind
tighter than `+` and `-`.

Values are sixteen-bit signed integers: `PRINT 30000+1` is `30001`, and
`PRINT 1000*20` is `20000`, neither of which the Kestrel's BASIC could hold.

### What it does not do yet

The honest list, because a manual that hides it is worse than useless:

- **no stored program** — there are no line numbers, so no `RUN`, `LIST`, `GOTO`,
  `GOSUB`, `FOR`/`NEXT` or `IF`. The line reader and the statement dispatcher are
  built for them: a line number in front of a statement is the next thing to go
  in;
- **no strings as values** — `PRINT "text"` works, `A$` does not;
- **no arrays, no functions** — no `DIM`, no `ABS` or `LEN`, no `DEF FN`;
- **no filesystem** — the disk device moves sectors, and Kestrel DOS 2.0's jump table
  entries answer `1` (an error) until it is written;
- **no cassette blocks** — the tape is a byte stream and the KERNAL has the
  registers, but the block format is not written.

---

## Writing for the machine

The interpreter is `roms/kestrel16/basic.s`; the KERNAL is `roms/kestrel16/kernal.s`; the map
and the services are `roms/kestrel16/kestrel16.inc`. All three are assembled by `kestrel16_asm`:

```bash
cmake --build build --target kestrel16_roms
./build/kestrel16_asm roms/kestrel16/basic.s --origin 65536 --size 65536 --bin basic.bin
```

### The dialect

`kestrel16_asm` is `kestrel_asm` with the 65816 added. Everything the Kestrel's assembler does it
does, plus:

| | |
| --- | --- |
| `LDA [$10]`, `LDA [$10],Y` | Direct page indirect long, and indexed |
| `LDA $10,S`, `LDA ($10,S),Y` | Stack relative, and its indirect form |
| `STA $7E:$2100` | A bank-qualified address; the value becomes 24-bit and picks a long mode |
| `MVN $7E,$7F` | Block move, destination bank first |
| `BRL`, `PER` | Sixteen-bit relative branches |
| `REP #$30` | The implied-with-a-byte forms |

Immediates are where the dialect earns its keep. The processor decides how wide
an immediate is from the M and X flags at run time, and the assembler cannot see
those flags, so the source says what it means:

- `LDA #$12` is **one byte**, the 6502's width and the default;
- `LDA.W #$1234` is **two**;
- `LDA.B #$12` says one byte again, for a reader;
- `LDA.L $1234` asks for the twenty-four-bit form;
- `LDX`/`LDY`/`CPX`/`CPY` take a **word** by default, because this machine keeps
  its index registers sixteen bits wide and every routine that widens them says
  `.B`.

An immediate that does not fit the width it was given is an error rather than a
silent truncation — a missing `.W` should not become a mystery on the screen.

### Two rules the ROMs follow

**The accumulator's width is a convention, not a guess.** `SEP #$20` makes the
accumulator a byte and `REP #$20` makes it a word; every immediate between a
`SEP` and its matching `REP` says `.B` or nothing, and every word immediate says
`.W`.

**The tube's position is the row and column pair.** `ZP_ROW` and `ZP_COL` are the
only copy; `CELLOFFSET` turns them into an address. An offset kept alongside them
drifts, and the drift shows up as text in the wrong place. The offset arithmetic
also uses its own scratch (`ZP_MUL1`, `ZP_MUL2`), because a print routine's
length counter lives in `ZP_TMP`.

---

## Building and testing

The machine is `src/kestrel16/`: the processor, the bus, the tube, the keyboard, the
disk and the tape. Its two suites are:

| Suite | What it proves |
| --- | --- |
| `kestrel16_asm_tests` | Every addressing mode and width rule of the dialect, byte for byte |
| `kestrel16_cpu_tests` | The processor: widths, banks, the direct page, arithmetic, cycles |
| `kestrel16_machine_tests` | The machine: the ROMs boot, and typing at it prints answers |

`kestrel16_machine_tests` never calls a ROM routine from C++. It types at the machine,
runs frames, and reads the screen back — the same way a person finds out what a
computer does.

```bash
cmake --build build -j && ctest --test-dir build --output-on-failure
```

---

## Where this is going

In order: the stored program and the control flow that needs one, strings as
values, arrays and functions, Kestrel DOS 2.0 in the KERNAL, and the window. The
Kestrel's [own manual](../kestrel/README.md) is the record of the same process one
machine earlier, and is what this one becomes when it has grown up.
