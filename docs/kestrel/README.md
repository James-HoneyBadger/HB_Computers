# The Kestrel Owner's Manual

A home computer from 1982, from HoneyBadger Systems. It has a 6502, 32 KiB of RAM
— the banner says 64K, and every machine of the year said something like it — a
40 × 25 tube, a disk drive seen one sector at a time, an 8 KiB KERNAL and a 16 KiB
socket for the language. The languages are machine code in ROM: nothing on the
screen is drawn by the host, and `READY.` is a string in a ROM image that the
processor executes.

```
        *** KESTREL BASIC V2.0 ROM ***
        64K RAM SYSTEM
        (C) 1982 HONEYBADGER SYSTEMS

READY.
█
```

---

## The machine

| | |
| --- | --- |
| Processor | MOS 6502 at 1 MHz, cycle-counted |
| Frame | 50 Hz, so 20 000 cycles a frame |
| RAM | 32 KiB, `$0000`–`$7FFF` — the banner says `64K RAM SYSTEM`, as banners did |
| Tube | 40 × 25 characters, an ASCII code each, plus one colour byte each |
| Keyboard | a 16-key controller buffer, drained by the frame interrupt |
| Sound | one latch byte; writing it is a note, and the window answers with a beep |
| Disk | 512 sectors of 256 bytes, a sector-level controller |
| ROM | 16 KiB socket at `$A000`, 8 KiB KERNAL at `$E000` |
| Cassette | a byte stream; the monitor ROM reads and writes blocks |

### The map

| Address | What is there |
| --- | --- |
| `$0000`–`$00FF` | Zero page: the KERNAL owns `$00`–`$1F`, the socket ROM the rest |
| `$0100`–`$01FF` | The processor's stack |
| `$0200`–`$02FF` | The input line |
| `$0300`–`$06FF` | Kestrel DOS buffers: the directory, the volume header, one data sector |
| `$0700`–`$070F` | The keyboard buffer the interrupt handler fills |
| `$0710`–`$073F` | Workspace: the name a disk call is looking for, the SAVE's counters, the four `DEF FN` definitions |
| `$0740`–`$07FF` | The digits of a number on its way to the screen, and room to grow |
| `$0801`–`$7EFF` | Program, variables and free memory |
| `$7F00`–`$7F47` | BASIC's value stack: twelve levels of six bytes |
| `$7F50`–`$7FBF` | Seven `FOR` frames of sixteen bytes |
| `$7FC0`–`$7FFF` | Sixteen `GOSUB` frames of four bytes |
| `$8000`–`$83E7` | The tube: 1000 character cells, row-major |
| `$8400`–`$87E7` | One colour byte per cell: ink in the high nibble, paper in the low |
| `$8800`–`$88FF` | The I/O page |
| `$9000`–`$9FFF` | A 4 KiB cartridge, if one is fitted |
| `$A000`–`$DFFF` | The language socket |
| `$E000`–`$FFFF` | The KERNAL |
| `$FFFA`–`$FFFF` | The 6502's interrupt vectors |

The map is written twice — `rust/crates/kestrel-machine/src/bus.rs` for the emulator
and `roms/kestrel/kestrel.inc` for the ROMs — and the two are changed together.

### The I/O page

A write is an output to a device and a read is an input from one; the page is not
storage.

| Offset | Register |
| --- | --- |
| `$00` | Keyboard data: the next key, `0` when there is none |
| `$01` | Keyboard status: `$FF` while a key is waiting |
| `$02` | Write anything to clear the keyboard buffer |
| `$03` | Cursor column |
| `$04` | Cursor row |
| `$05` | Ink |
| `$06` | Paper |
| `$07` | Border |
| `$08` | Sound: a write plays the note, a read answers `0` |
| `$09` / `$0A` | The frame counter, 50 per second, low byte first |
| `$0B` | `$FF` when a frame has passed; write `0` to clear it |
| `$0C` / `$0D` | Disk sector number, low byte first |
| `$0E` | Disk command: `1` read, `2` write, `3` report the sector count |
| `$0F` | Disk status: `0` fine, `$80` no disk, `$81` bad sector, `$82` bad command |
| `$10` / `$11` | Where in RAM the sector goes or comes from |
| `$12` / `$13` | The volume's sector count, as command `3` reports it |
| `$14` | A byte from the noise generator; reading advances it |
| `$15` | The last key the controller saw |
| `$16` | `$FF` once the screen has scrolled; write `0` to clear it |
| `$17` | `1` while the cursor is lit |
| `$18` | Tape data: a write records a byte, a read takes the next |
| `$19` | Tape status: bit 7 present, 6 playing, 5 recording, 4 more to read |
| `$1A` | Tape control: `1` play, `2` record, `3` stop, `4` rewind |

### Colours

Sixteen codes, and the attribute byte of a cell holds two of them: ink in the high
nibble, paper in the low. A write to `$05` or `$06` chooses the ink and the paper
for everything printed after it, and four bits is the whole of the choice.

| Code | | Code | | Code | | Code | |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `0` | black | `4` | purple | `8` | orange | `12` | grey |
| `1` | white | `5` | green | `9` | brown | `13` | light green |
| `2` | red | `6` | blue | `10` | light red | `14` | light blue |
| `3` | cyan | `7` | yellow | `11` | dark grey | `15` | light grey |

The two halves of the byte are the machine's; which red, and how bright, is the
tube's business, and the window draws a muted set that suits a 1982 monitor.

---

## Switching on

The processor resets through `$FFFC`, finds the KERNAL's reset routine, clears the
screen, prints the banner and hands the machine to the ROM in the socket. The
socket ROM prints ``READY.` and waits for a line. In the window, `Ctrl+Q` closes it.

**BREAK** is a non-maskable interrupt that warm-starts the machine: it abandons a
running program, prints `READY.` and leaves the program in memory, ready to be
`LIST`ed or `RUN` again. The window presses it from **Machine ▸ BREAK**, or with
the `Pause` key.

### The keyboard

| Key | Sends |
| --- | --- |
| A–Z, 0–9, punctuation | its ASCII code |
| Return | 13 |
| Backspace | 8 |
| Escape | 27 |
| Arrows | 28–31 |
| Delete | 127 |

The letters reach the screen as typed, upper or lower: the machine is case-blind
about what it reads and matches `print` to `PRINT`, so a program can be typed the
way a person writes.

---

## The KERNAL

The KERNAL is always there and knows nothing about any language. Its services are
fixed addresses, which is how the socket ROM reaches the machine:

| Address | Service |
| --- | --- |
| `$E000` | `CHROUT` — A = character |
| `$E003` | `GETIN` — A = the next key, or `0` |
| `$E006` | `CHRIN` — the next key, waiting for one |
| `$E009` | `CLS` |
| `$E00C` | `HOME` |
| `$E00F` | `PRINT` — the zero-terminated string at `$0A`/`$0B` |
| `$E012` | `SCROLL` |
| `$E015` | `LOCATE` — X = column, Y = row |
| `$E018` | `GETLINE` — a line into the buffer at `$0200`, its length in `$15` |
| `$E01B` | `READSECT` — A/X = sector low/high, the buffer at `$06`/`$07` |
| `$E01E` | `WRITESECT` — the same three, the other way round |
| `$E021` | `CURSORON` — A = `1` for a solid cursor, `0` for none |
| `$E024` | `DOSCATALOG` — list the volume |
| `$E027` | `DOSFIND` — the name at `$0710`; A = its entry, or `$FF` |
| `$E02A` | `DOSLOAD` — load to `$18`/`$19`, which comes back holding the end |
| `$E02D` | `DOSSAVE` — save from `$18`/`$19` to `$1B`/`$1C` |
| `$E030` | `DOSDELETE` — remove the name at `$0710` |
| `$E033` | `DOSSTATUS` — A = `0`, or the error code below |
| `$E036` | `DOSFORMAT` — a blank volume |

Each of the disk words returns its reason in A: `0` fine, `1` no disk, `2` no
such file, `3` no room, `4` the disk itself failed.

### The frame

Fifty times a second the machine raises its interrupt. The handler counts frames,
blinks the cursor, and empties the keyboard controller into the KERNAL's buffer —
which it must, because the controller's interrupt line stays high until its last
key is read.

---

## Kestrel DOS 1.0

The disk is 512 sectors of 256 bytes, and the controller moves one sector at a
time (see `$0C`–`$11` above). Everything above that is Kestrel DOS, in the KERNAL:

- **Sector 0** is the volume header: the signature, the volume name and the
  sector count.
- **Sectors 1–2** are the directory: 32 entries of 16 bytes — name, first sector,
  length, and the file's type.
- **Sector 3 onwards** hold files: 252 bytes of data, a pointer to the next
  sector, and how much of this one is used.
- A freshly formatted volume is named `KESWARP`. BASIC has no `FORMAT`; the disk
  arrives formatted and is formatted again from the window's **Media ▸ Floppy
  disk ▸ Format disk**.

From BASIC: `SAVE "NAME"`, `LOAD "NAME"`, `CATALOG`, and `DELETE` removes a line
rather than a file. `CATALOG` lists the volume; a file that will not load reports
its error on the `READY.` line — `?FILE NOT FOUND` when the directory has no such
name.

Saving over a name that is already on the volume replaces it: the old file's
sectors go back on the free map and its entry is cleared before the new one is
written, so a volume never holds two files of the same name.

---

## Kestrel BASIC V2.0

The language in the socket. Type a line and press Return; a line that starts with
a number is stored in the program, and a line without one is run at once.

### Statements

| Group | Words |
| --- | --- |
| Program | `LIST [n]`, `RUN`, `NEW`, `DELETE n[,m]`, `SAVE "NAME"`, `LOAD "NAME"`, `CATALOG` |
| Output and input | `PRINT` (`?`), `INPUT`, `GET` |
| Variables and data | `LET`, `DIM`, `READ`, `DATA`, `RESTORE` |
| Control flow | `IF … THEN … ELSE`, `FOR … TO … STEP … NEXT`, `GOTO`, `GOSUB`, `RETURN`, `ON … GOTO`, `ON … GOSUB` |
| Other | `REM`, `END`, `STOP`, `CONT`, `POKE`, `CLS`, `HELP` |
| Your own functions | `DEF FN A(X)=X*X+1`, then `PRINT FN A(3)` |

`HELP` prints the command list, which is where this table comes from.

A definition is kept as a pointer at the expression, which stays where it was
written, so a function has to be *defined in the program*:

```basic
10 DEF FN A(X)=X*X+1
20 PRINT FN A(3)
RUN
10
```

Typed at the prompt, `DEF FN` points into the input buffer and is worthless by
the next line — the text it named has gone. Write the definitions in the program.

A `FOR` whose start is already past its limit runs its body no times, and leaves
the variable holding that start, so `FOR J=I*I TO 100` is safe even when `I*I` has
run past the end of the array it indexes. The machine walks forward for the
matching `NEXT` to find out where the body ends, counting the loops it passes
through on the way, and a `NEXT` inside a remark or a quoted string is not
mistaken for it. `STEP` may be negative; `STEP 0` loops for ever, as it did then.

### Functions

| | |
| --- | --- |
| Numeric | `ABS`, `INT`, `SGN`, `SQR`, `RND`, `PEEK` |
| Transcendental | `SIN`, `COS`, `TAN`, `ATN`, `EXP`, `LOG` |
| String | `LEN`, `ASC`, `CHR$`, `STR$`, `VAL`, `LEFT$`, `RIGHT$`, `MID$` |

### Expressions

`+ - * / ^`, parentheses, unary minus, and the comparisons `=` `<>` `<` `>` `<=`
`>=`. `AND`, `OR` and `NOT` combine comparisons. `+` also joins two strings, and
strings compare with the same operators (the shorter sorts first when one is a
prefix of the other). Numbers are five-byte floating point: `1`, `2.5`, `-3` and
`1E3` all parse. The format holds about `2.9E-39` to `1.7E38`: past the top it
says `?OVERFLOW`, and past the bottom the answer is `0`.

### Variables and arrays

`A`–`Z` and `A0`–`Z9`, each a number or — with a `$` — a string. Arrays are
automatic at eleven elements: `A(1)`, `M(rows,columns)`, `DIM A$(20)`, up to four
dimensions. `DIM` twice is an error worth having: `?REDIMENSIONED ARRAY`.

### Errors

An error prints its reason and the line it happened in — `?SYNTAX ERROR IN 40` —
and a `STOP` prints `BREAK IN 120` and leaves the program where it was. The
reasons the table has:

| | | |
| --- | --- | --- |
| `SYNTAX` | `NEXT WITHOUT FOR` | `RETURN WITHOUT GOSUB` |
| `OUT OF DATA` | `IF WITHOUT THEN` | `REDIMENSIONED ARRAY` |
| `DIVISION BY ZERO` | `OUT OF MEMORY` | `UNDEF'D STATEMENT` |
| `BAD SUBSCRIPT` | `ILLEGAL QUANTITY` | `BAD FILE NAME` |
| `FILE NOT FOUND` | `DISK ERROR` | `BREAK` |
| `OVERFLOW` | `UNDEF'D FUNCTION` | `BAD DATA` |
| `STRING TOO LONG` | `CAN'T CONTINUE` | `NO FILE` |
| `TYPE MISMATCH` | `NOT IN THIS ROM YET` | |

`CONT` resumes after a `STOP` or a BREAK, unless the program has been edited.

### Worked example

A sieve of Eratosthenes, the program every 1982 magazine carried:

```basic
10 REM PRIME SIEVE TO 100
20 DIM P(100)
30 FOR I=2 TO 100
40 IF P(I) THEN 70
50 PRINT I;
60 FOR J=I*I TO 100 STEP I: P(J)=1: NEXT J
70 NEXT I
80 PRINT
```

```
RUN
2357111317192329313741434753596167717379
838997
```

Line 60 is the interesting one: once `I` passes ten, `I*I` is past the end of
the array, and the loop runs no times at all rather than once with a subscript
out of range.

The primes share a line because line 50 is `PRINT I;` and not `PRINT I`: a
trailing `;` leaves the cursor where it is, so the next number follows the last.
The same holds at the prompt, where `PRINT 5;` prints `5READY.` on one line.

---

## The monitor ROM

The other socket ROM. `HELP` is not in it; its own words are:

| Command | Does |
| --- | --- |
| `M aaaa [nn]` | Show memory from `aaaa`, `nn` bytes |
| `: aaaa bb …` | Deposit bytes at `aaaa` |
| `G aaaa` | Go to `aaaa` |
| `R` | Show the registers |
| `S aaaa bbbb` | Save `aaaa`–`bbbb` to the cassette |
| `L` | Load a block back from the cassette |

It is how a machine-code program is written and run on the machine itself, and
how the cassette is used at all: BASIC has no tape words.

`S aaaa bbbb` takes the block from `aaaa` to `bbbb` *inclusive*, so `S 0900
0902` writes three bytes behind a four-byte header — the load address and the
length — and `L` puts them back where they came from. Both words need a cassette
in the deck: the window inserts one with **Media ▸ Tape ▸ Insert tape…**, and
`kestrel --tape` takes an image or hands the machine a blank one. With the deck
empty, `S` and `L` both say `no tape`.

`G aaaa` jumps to the address; an `RTS` there returns to the prompt, so a routine
can be deposited and tried. `R` prints the registers last seen by the frame
interrupt — the only register dump a monitor without a `BRK` vector of its own
can give.

The socket holds one ROM at a time, and which one it is changes the machine: the
**ROM socket** group on the settings window's **Machine** page swaps them and
cold-boots, and `kestrel --rom monitor` does the same from the command line.

---

## The other ROMs in the socket

BASIC and the monitor were the two in the box. The socket took anything else that
came on a chip, and this repository has two more — a language for schools and a
ROM for the workshop. `roms/README.md` is the catalogue.

| ROM | `--rom` | What it is |
| --- | --- | --- |
| Kestrel BASIC V2.0 | `basic` | the language the machine is switched on with |
| Kestrel Monitor V1.0 | `monitor` | the processor's view, above |
| Kestrel PILOT V1.0 | `pilot` | the teaching language: `T:` says, `A:` asks, `M:` marks, `Y:`/`N:`/`J:` go somewhere |
| Kestrel Diagnostics V1.0 | `diagnostics` | the service ROM: memory with the address that failed, the tube, a note to listen for, and every key with its code |

```sh
kestrel --rom pilot
kestrel --languages            # what the socket could take
```

PILOT keeps a lesson as numbered lines of one statement each, and a line typed
without a number is obeyed at once:

```pilot
10 T:WHAT IS THE CAPITAL OF FRANCE?
20 A:C
30 M:PARIS
40 Y:RIGHT
50 T:NO, IT IS PARIS.
60 E:
70 :RIGHT
80 T:THAT IS RIGHT.
90 E:
```

The diagnostics ROM is what a shop fitted to a machine that came back faulty. It
prints one line per test, and where memory failed it prints the address:

```
*** KESTREL DIAGNOSTICS V1.0 ***

RAM  $0800-$7FFF  OK
TUBE 40 X 25      OK
SOUND             LISTEN FOR THE NOTE
KEYBOARD          PRESS ANY KEY
```

---

## What the machine does not have

The limits, stated plainly:

- **no tape words in BASIC** — the cassette is the monitor's (see above);
- **no `WHILE`, no procedures** — this is 1982 BASIC, and `GOSUB` is the idiom;
- **no `MERGE`, no renumbering** — `DELETE` and retyping are the tools;
- **no case folding** — keywords are matched without regard to case, so
  `print` is `PRINT`; everything else is stored as typed, and a program lists
  back in the case it was written in;
- **one language at a time** — the socket holds one ROM, and changing it resets
  the machine.

---

## Writing for the machine

The language is `roms/kestrel/basic.s`; the KERNAL is `roms/kestrel/kernal.s`;
Kestrel DOS is `roms/kestrel/dos.inc`; the map and the constants are
`roms/kestrel/kestrel.inc`. `kestrel_asm` assembles them, and the machine does it
for you when it starts:

```bash
./start.sh cli --type "PRINT 2+2"
```

To assemble an image by hand:

```bash
cd roms/kestrel
../../rust/target/release/kestrel_asm basic.s --name basic \
    --origin 0xA000 --size 16384 --fill 0xFF --bin basic.bin --listing basic.lst
```

A program you write in assembly is assembled into a ROM image and either put in
the socket or fitted as a cartridge; from BASIC, the monitor's `G` starts code
that is already in memory. [The tools page](../TOOLS.md) covers both assemblers.

---

## Disks and tapes

The drive takes a volume image of the whole disk -- 512 sectors of 256 bytes, so
131072 bytes -- and the deck takes any byte stream as a tape:

```bash
./start.sh cli --disk myvolume.dsk --tape mysave.tap --type "PRINT 1"
```

A disk is a disk: what the machine writes is in the image when it is mounted
again. A file of the wrong size is refused rather than half-loaded, because a
half-loaded volume is worse than a missing one.

## Colophon

The machine is `Machine` built for `Model::Kestrel`. Its tests type at it and read
the screen back — `cpu6502.rs` for the processor, `machine.rs` for the machine and
its ROMs, `kestrel-asm` for the dialect. The 1992 machine is
[the Kestrel 16](../kestrel16/README.md).
