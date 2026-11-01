# The Kestrel Owner's Manual

A home computer from 1982, from HoneyBadger Systems. It has a 6502, 64K, a
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
| RAM | 32 KiB, `$0000`–`$7FFF` |
| Tube | 40 × 25 characters, an ASCII code each, plus one colour byte each |
| Keyboard | a 16-key controller buffer, drained by the frame interrupt |
| Sound | one latch byte: the note the host plays |
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
| `$0801`–`$7EFF` | Program, variables and free memory |
| `$7F00`–`$7FFF` | The stacks and the top of free RAM |
| `$8000`–`$83E7` | The tube: 1000 character cells, row-major |
| `$8400`–`$87E7` | One colour byte per cell: ink in the high nibble, paper in the low |
| `$8800`–`$88FF` | The I/O page |
| `$9000`–`$9FFF` | A 4 KiB cartridge, if one is fitted |
| `$A000`–`$DFFF` | The language socket |
| `$E000`–`$FFFF` | The KERNAL |
| `$FFFA`–`$FFFF` | The 6502's interrupt vectors |

The map is written twice — `include/kestrel/model1982/map.hpp` for the emulator
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

Sixteen, packed into the attribute byte: the low three bits are the guns, bit 3 is
brightness. `0` black, `1` blue, `2` red, `3` magenta, `4` green, `5` cyan,
`6` yellow, `7` light grey, and `8`–`15` the same again at full brightness.

---

## Switching on

The processor resets through `$FFFC`, finds the KERNAL's reset routine, clears the
screen, prints the banner and hands the machine to the ROM in the socket. The
socket ROM prints `READY.` and waits for a line. `Ctrl+Q` leaves the machine on;
the BREAK key is a non-maskable interrupt that warm-starts it.

### The keyboard

| Key | Sends |
| --- | --- |
| A–Z, 0–9, punctuation | its ASCII code |
| Return | 13 |
| Backspace | 8 |
| Escape | 27 |
| Arrows | 28–31 |
| Delete | 127 |

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
| `$E00F` | `PRINT` — a pointer in `$10`, a length in X |
| `$E012` | `SCROLL` |
| `$E015` | `LOCATE` — X = column, Y = row |
| `$E018` | `GETLINE` — read into the buffer at `$10`, length in X |
| `$E01B` | `READSECT` — X = sector, buffer at `$10` |
| `$E01E` | `WRITESECT` |
| `$E021` | `CURSORON` |
| `$E024` | `DOSCATALOG` |
| `$E027` | `DOSFIND` |
| `$E02A` | `DOSLOAD` |
| `$E02D` | `DOSSAVE` |
| `$E030` | `DOSDELETE` |
| `$E033` | `DOSSTATUS` |
| `$E036` | `DOSFORMAT` |

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
- A freshly formatted volume is named `KESWARP`.

From BASIC: `SAVE "NAME"`, `LOAD "NAME"`, `CATALOG`, and `DELETE` removes a line
rather than a file. `CATALOG` lists the volume; a file that will not load reports
its error on the `READY.` line.

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
`1E3` all parse.

### Variables and arrays

`A`–`Z` and `A0`–`Z9`, each a number or — with a `$` — a string. Arrays are
automatic at eleven elements: `A(1)`, `M(rows,columns)`, `DIM A$(20)`, up to four
dimensions. `DIM` twice is an error worth having: `?REDIMENSIONED ARRAY`.

### Errors

The machine prints the reason and where it happened: `SYNTAX ERROR IN 40`, or
`BREAK ERROR IN 120` after `STOP`. The reasons it has:

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

```basic
10 REM NAMES
20 DIM N$(3)
30 N$(1)="ADA": N$(2)="GRACE": N$(3)="KATHERINE"
40 FOR I=1 TO 3
50 GOSUB 100
60 NEXT I
70 END
100 PRINT "HELLO, "; N$(I)
110 RETURN
```

```
RUN
HELLO, ADA
HELLO, GRACE
HELLO, KATHERINE
```

A `DIM`ed string array, a loop, a subroutine and a return: most of the machine
in eight lines.

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

---

## What the machine does not have

The honest list, because a manual that hides it is worse than useless:

- **no tape words in BASIC** — the cassette is the monitor's (see above);
- **no `WHILE`, no procedures** — this is 1982 BASIC, and `GOSUB` is the idiom;
- **no `MERGE`, no renumbering** — `DELETE` and retyping are the tools;
- **no lowercase input** — the machine uppercases what it reads;
- **one language at a time** — the socket holds one ROM, and changing it resets
  the machine.

### A fault, not a feature

A comparison in an expression (`1=1`, `A<B`, and so on) after an array has been
`DIM`ed overwrites the array's descriptor in the heap: the array stops being
found and a later `DIM` of the same name is allowed again. `IF A=B THEN 100`
still jumps — the test itself is fine — but any array the program holds is gone
from that point on. Until the ROM is mended, a program that needs both arrays and
comparisons should test with arithmetic instead:

```basic
100 REM INSTEAD OF  IF A=B THEN 200
110 T=A-B: IF T THEN 130
120 GOTO 200
130 REM ... continue
```

---

## Writing for the machine

The language is `roms/kestrel/basic.s`; the KERNAL is `roms/kestrel/kernal.s`;
Kestrel DOS is `roms/kestrel/dos.s`; the map and the constants are
`roms/kestrel/kestrel.inc`. `kestrel_asm` assembles them, and the build does it
for you:

```bash
cmake --build build --target kestrel_roms
```

A program you write in assembly is assembled into a ROM image and either put in
the socket or fitted as a cartridge; from BASIC, the monitor's `G` starts code
that is already in memory. [The tools page](../TOOLS.md) covers both assemblers.

---

## Colophon

The machine is `kestrel::Machine` built for `Model::Kestrel`. Its tests type at it
and read the screen back — `kestrel_cpu_tests` for the processor,
`kestrel_machine_tests` for the machine and its ROMs, `kestrel_asm_tests` for the
dialect. The 1992 machine is [the Kestrel 16](../kestrel16/README.md).
