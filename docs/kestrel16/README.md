# The Kestrel 16 Owner's Manual

HoneyBadger Systems' 1992 computer, and the Kestrel's successor. Same idea, ten
years on: a processor, a ROM socket, a tube, a keyboard and a disk, with the
language in ROM rather than in the host. What the decade bought is width — a
65816, a megabyte of banked memory, and sixteen-bit arithmetic in a language that
no longer shares a socket with the kernel.

```
KESTREL 16 BASIC 2.0
READY.
PRINT 2+2
4
READY.
```

---

## The machine

| | |
| --- | --- |
| Processor | WDC 65C816 at 8 MHz, emulation and native modes |
| Frame | 50 Hz, so 160 000 cycles a frame |
| RAM | 1 MiB, spread over banks `$00`–`$0F` under the ROM and I/O windows |
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
| `$00` | `$0200`–`$07FF` | System RAM: the keyboard buffer at `$0300`, the input line at `$0400`, the line buffer at `$0500`, and the language's variables |
| `$00` | `$0800`–`$7EFF` | Free RAM, nearly 30 KiB; `MEMTOP` answers `$7F00` |
| `$00` | `$8000`–`$8F9F` | The tube: 4000 character cells, row-major |
| `$00` | `$8FA0`–`$9F3F` | One attribute byte per cell |
| `$00` | `$A000`–`$A0FF` | The I/O page |
| `$00` | `$A100`–`$BFFF` | Nothing responds: reads are zero, writes are lost |
| `$00` | `$C000`–`$DFFF` | Cartridge ROM, 8 KiB |
| `$00` | `$E000`–`$FFFF` | The KERNAL |
| `$01` | `$0000`–`$FFFF` | The language ROM |
| `$04`–`$07` | | A cartridge, 256 KiB |
| `$02` | | RAM: the filesystem's header, directory and one sector's buffer |
| `$03` | | RAM: the language's strings and arrays, in a bank of their own |
| `$08`–`$0F` | | RAM |
| `$04`–`$07` | | RAM too, when no cartridge is fitted |

A megabyte is all sixteen banks; what the table above lists as ROM, cartridge and
I/O is what stands in front of it.

The map is written twice — `rust/crates/kestrel-machine/src/bus.rs` for the emulator
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
| `$0B` | Bit 7 set when a frame has passed; write `0` to clear it |
| `$0C` / `$0D` | Disk sector |
| `$0E` | Disk command: `1` read, `2` write, `3` format, `4` query |
| `$0F` | Disk status: bit 7 done, bit 2 present, bit 1 error. A command runs inside the write that starts it, so there is no busy bit to see |
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
again. The window presses it from **Machine ▸ BREAK**, or with the `Pause` key.

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
| `$E018` | `GETLINE` — keys into the buffer `$10` points at; the length comes back in `$1A` |
| `$E01B` | `READSECT` — X = sector, buffer at `$10` |
| `$E01E` | `WRITESECT` |
| `$E021` | `CURSORON` |
| `$E024` | `DOSCATALOG` — the volume's name, and a line for each file |
| `$E027` | `DOSFIND` — A = a name (see below); X = its entry, `$69`/`$6A` = its first sector, carry set when the volume has no such name |
| `$E02A` | `DOSLOAD` — A = a name; the program store becomes that file |
| `$E02D` | `DOSSAVE` — A = a name; the program store becomes that file |
| `$E030` | `DOSDELETE` — A = a name; its sectors go back to the free map |
| `$E033` | `DOSSTATUS` — `0` when the disk controller is content |
| `$E036` | `DOSFORMAT` — write a new volume over whatever is on the disk |
| `$E039` | `SETINK` |
| `$E03C` | `SETPAPER` |
| `$E03F` | `SOUND` |
| `$E042` | `FRAMES` — the frame counter |
| `$E045` | `MEMTOP` — X = `$7F00`, the first byte above the free RAM |
| `$E048` | `PEEKBYTE` — A = bank, X = address |
| `$E04B` | `POKEBYTE` — A = bank, X = address, Y = value |
| `$E04E` | `GETKEY` |
| `$E051` | `NEWLINE` |

A `JSR` stays in the program bank on this processor, so the language in bank one
cannot use that table. Beside it, at `$00:FF00`, is the same list again — four
bytes an entry, `JSR` and `RTL` — and a caller in another bank reaches it with
`JSL`. Both tables exist because the processor makes them necessary.

The four services that take a name take it the same way: A is a sixteen-bit
pointer to sixteen bytes in bank zero, upper case and padded with spaces, which
is what a directory entry is. They return a status in A — `0` for what was asked
for, `1` for a name that is not on the volume, `2` for a controller that would
not, `3` for a volume with no room — and carry is set when that status is not
zero.

### The frame

Fifty times a second the machine raises its interrupt. The handler counts frames,
puts the cursor where the tube should draw it, blinks it every thirty-two frames,
and empties the keyboard controller into the KERNAL's buffer.

---

## Kestrel 16 BASIC 2.0

A line-oriented BASIC with sixteen-bit integers, in the tradition the decade
inherited: type a line, press Return, read the answer.

```
READY.
PRINT 2+2
4
READY.
LET A=41
READY.
PRINT "THE ANSWER IS ";A+1
THE ANSWER IS 42
```

### What the language does today

| Statement | |
| --- | --- |
| `PRINT` | Expressions and strings, separated by `;` (no space) and `,` (a space). A trailing `;` leaves the line open. `?` is shorthand |
| `RUN` [`n`] | Run the program, or start it at line `n` |
| `LIST` [`n`] | Show the program, or from line `n` on |
| `GOTO n` | Continue at line `n` |
| `IF cond THEN … [ELSE …]` | One branch or the other; a line number after `THEN` jumps |
| `GOSUB n` / `RETURN` | Sixteen levels of subroutine |
| `FOR v=a TO b [STEP s]` … `NEXT v` | A loop; `s` may be positive or negative, defaults to `1`, and may not be zero |
| `NEW` | Throw the program and the variables away |
| `LET` | `LET A=41`, or `A=41`; variables are `A` to `Z`, sixteen bits each |
| `LET A$="text"` | a string variable, `A$` to `Z$`, up to thirty-one characters |
| `LEN(A$)` | how many characters a string has |
| `A$+B$` | the two joined, in a temporary of thirty-one characters |
| `LEFT$(A$,n)` / `RIGHT$(A$,n)` / `MID$(A$,n,m)` | part of a string; `MID$` counts its first character as one, and a count past the end is as much as there is |
| `CHR$(n)` / `ASC(A$)` | the character with that number, and the number of the first character |
| `STR$(n)` / `VAL(A$)` | the digits of a number, and the number those digits begin with |
| `PEEK(addr)` / `POKE addr,value` | one byte of bank zero, which is where the tube, the I/O page and the program are |
| `DIM A(10)` | an array of eleven words, `A(0)` to `A(10)`; one dimension, and the `DIM` comes first |
| `CLS` | Clear the screen and print the banner |
| `REM` | A remark |
| `END` | Finish the line |
| `NEW` | Start again |

A colon separates statements, so a line may hold several:
`PRINT "A";: PRINT "B"` prints `AB`.

Expressions are `+ - * /` with parentheses and unary minus; `*` and `/` bind
tighter than `+` and `-`. Comparisons (`=` `<>` `<` `>` `<=` `>=`) give `-1` or
`0`, and `AND`, `OR` and `NOT` combine them. Values are sixteen-bit signed integers, so
`PRINT 30000+1` is `30001` and `PRINT 1000*20` is `20000` — neither of which the
Kestrel's BASIC could hold.

A `PRINT` item has to be something the machine can read. It is evaluated first and
printed only if it is whole, so `PRINT A$` — which is `A` followed by a character
that means nothing here — says `?WHAT?` and prints no value at all, and
`PRINT 1+2` still prints `3`.

Anything else is a mistake, and the machine says so:

```
READY.
WIBBLE
?WHAT?
```

### The stored program

A line that begins with a number is a program line: it is stored, not run. What
follows the number is what will run later, and a line typed again replaces the
old one. A number on its own removes that line.

```basic
10 PRINT "A"
20 GOTO 40
30 PRINT "B"
40 PRINT "C"
RUN
A
C
```

Lines are held in bank zero from `$0800`, as records of a number, the text and a
zero, in ascending order — there are no link pointers to be left pointing into a
line that was edited, and a line is found by scanning. The program may not pass
`$7E00`; past it the machine says `?NO ROOM`. A jump to a line that is not there
says `?NO LINE` and stops the program, and so do a `NEXT` with no `FOR` behind it
(`?NEXT WITHOUT FOR`) and a `FOR` with no `NEXT` to reach (`?FOR WITHOUT NEXT`).

A loop whose start is already past its limit has no passes in it at all, and its
body is skipped rather than run once:

```basic
10 FOR I=5 TO 1
20 PRINT "NEVER"
30 NEXT I
40 PRINT "DONE"
RUN
DONE
```

### What it does not do yet

The honest list, because a manual that hides it is worse than useless:

- **loops that span lines belong in a program** — a loop typed at the prompt runs
  only as far as the end of its line, because each new line starts with no loops
  and no subroutines pending. A whole loop on one line, colons and all, runs;
- **strings, in part** — a string variable holds up to thirty-one characters,
  and can be assigned, printed, compared, joined, sliced and measured;
- **arrays, in part** — `DIM A(10)` and `A(i)`, one dimension and whole words.
  There are no string arrays and no `DIM` of more than one subscript, and a
  `DIM` that asks for more elements than the letter already has takes a fresh
  run from the heap and does not give the old one back;
- **no other functions** — no `ABS`, no `DEF FN`;
- **a filesystem, in part** — `FORMAT`, `CATALOG`, `SAVE`, `LOAD` and `DELETE`
  work; there is no `FIND` statement. The volume format below is fixed, so
  anything written against it keeps working;
- **no cassette blocks** — the tape is a byte stream and the KERNAL has the
  registers, but the block format is not written.

---

## The volume format

Kestrel DOS 2.0 keeps its system area in the first two sectors of a 4096-sector
volume. The header is sector 0:

| Offset | Bytes | What |
| --- | --- | --- |
| 0 | 8 | `"HBDOS20"` and a zero |
| 8 | 16 | the volume name, space padded |
| 24 | 2 | the sector count, low byte first |
| 26 | 6 | reserved |
| 32 | 480 | the free map: one bit per sector, set means taken — the same way round as the 1982 DOS |

The map is nearly a whole sector because 4096 sectors need 4096 bits. That is the
one place this filesystem cannot simply be the 1982 one with larger numbers, and
it is why the map starts at a fixed offset rather than following the name. Four
hundred and eighty bytes hold 3840 bits, so the top 256 sectors of the volume
have no bit of their own and are never allocated.

Sector 1 is the directory: sixteen entries of thirty-two bytes.

| Offset | Bytes | What |
| --- | --- | --- |
| 0 | 16 | the file's name, space padded; a zero in the first byte means the entry is free |
| 16 | 2 | its first sector |
| 18 | 2 | how many sectors it occupies |
| 20 | 12 | reserved |

`FORMAT` writes both sectors, so a freshly formatted volume is a header naming
itself and sixteen free entries. `CATALOG` reads the header back and prints the
volume name between brackets.

A file is a run of consecutive sectors, never a chain, so a save takes the lowest
run on the map that is long enough for it and writes the whole file in order. A
volume that has had files deleted therefore fills its holes before it grows, and
a file that will not fit in any hole is refused rather than split.

## Writing for the machine

The interpreter is `roms/kestrel16/basic.s`; the KERNAL is
`roms/kestrel16/kernal.s`; the map and the services are
`roms/kestrel16/kestrel16.inc`. All three are assembled by `kestrel16_asm`, and
the machine does it when it starts:

```bash
./start.sh cli --model kestrel16 --type "PRINT 2+2"
```

To assemble an image by hand -- into a file you can look at, or put on a
cartridge:

```bash
cd roms/kestrel16
../../rust/target/release/kestrel16_asm basic.s --name basic \
    --origin 65536 --size 65536 --fill 0xFF --bin basic.bin --listing basic.lst
```

### Three rules the ROMs follow

**The accumulator's width is a convention, not a guess.** `SEP #$20` makes the
accumulator a byte and `REP #$20` makes it a word; every immediate between a
`SEP` and its matching `REP` says `.B` or nothing, and every word immediate says
`.W`. The assembler cannot see the flags, so the source says what it means, and
an immediate that does not fit its width is an error rather than a silent
truncation. The same is true of an address: `.L` asks for the long form, and a
mnemonic that has none for that operand is refused rather than quietly given the
absolute one, which would address the data bank instead.

**A memory increment is as wide as the accumulator.** In eight-bit mode `INC addr`
moves one byte and the byte above it is left alone, so a sixteen-bit pointer that
passes a page boundary starts the page again. Every pointer here is stepped by a
routine that sets the width first — `SRC_STEP` for the line being read, `DOS_STEP`
for the filesystem's — and a bare `INC` on a word is a bug that shows itself only
in a program long enough to cross a page.

**The tube's position is the row and column pair.** `ZP_ROW` and `ZP_COL` are the
only copy and `CELLOFFSET` turns them into an address. An offset kept alongside
them drifts, and the drift shows up as text in the wrong place.

[The tools page](../TOOLS.md) covers the dialect in full.

---

## Colophon

The machine is `Machine` built for `Model::Kestrel16`. Its tests type at
it and read the screen back: the tests in `cpu65816.rs` for the processor,
`machine.rs` for the machine and its ROMs, `kestrel16-asm` for
the dialect. The 1982 machine is [the Kestrel](../kestrel/README.md).
