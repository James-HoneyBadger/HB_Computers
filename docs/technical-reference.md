# Technical Reference

The machines as a programmer meets them: the memory maps, the registers, the
formats on the media, the way the processors behave, and the order in which the
bus answers. Every table here is written against the sources named beside it, and
the maps exist twice on purpose — once in C++ for the emulator, once in assembly
for the ROMs — so the two are changed together.

| Fact | Written in |
| --- | --- |
| The 1982 map and registers | `include/kestrel/model1982/map.hpp`, `roms/kestrel/kestrel.inc` |
| The 1992 map and registers | `include/kestrel/model1992/map.hpp`, `roms/kestrel16/kestrel16.inc` |
| The profile table, the decode, the devices | `src/kestrel/machine.cpp`, `src/kestrel/devices.cpp` |
| The processors | `src/kestrel/model1982/cpu6502.cpp`, `src/kestrel/model1992/cpu65816.cpp` |

---

## Contents

- [The two profiles](#the-two-profiles)
- [The Kestrel's map](#the-kestrels-map)
- [The Kestrel 16's map](#the-kestrel-16s-map)
- [The I/O pages](#the-io-pages)
- [Colour](#colour)
- [The bus, and the order it answers in](#the-bus-and-the-order-it-answers-in)
- [Interrupts, the frame and the clock](#interrupts-the-frame-and-the-clock)
- [The keyboard](#the-keyboard)
- [The disk](#the-disk)
- [The cassette](#the-cassette)
- [The ROMs](#the-roms)
- [The processors](#the-processors)
- [What the machines do not have](#what-the-machines-do-not-have)

---

## The two profiles

Everything that differs between the machines is one table, `makeProfile` in
`src/kestrel/machine.cpp`. Nothing else in the emulator knows which machine it
is running.

| | Kestrel | Kestrel 16 |
| --- | --- | --- |
| Clock | 1 000 000 Hz | 8 000 000 Hz |
| Frame rate | 50 Hz | 50 Hz |
| Cycles a frame | 20 000 | 160 000 |
| RAM | 32 KiB (`$0000`–`$7FFF`) | 1 MiB, banks `$00`, `$08`–`$0F` |
| Screen | 40 × 25 at `$8000` | 80 × 50 at `$00:8000` |
| Attributes | 1000 bytes at `$8400` | 4000 bytes at `$00:8FA0` |
| I/O page | `$8800`, 256 bytes | `$00:A000`, 256 bytes |
| Cartridge | 4 KiB at `$9000` | 8 KiB at `$00:C000`, RAM in `$04`–`$07` |
| KERNAL | 8 KiB at `$E000` | 8 KiB at `$00:E000` |
| Language | 16 KiB at `$A000` | 64 KiB in bank `$01` |
| Disk | 512 × 256 bytes | 4096 × 512 bytes |
| Keyboard controller | 16 keys | 32 keys |

The clock is what the host paces the processor to at 1, 2, 4 or 8 MHz; the
processor itself is the same one at any pace. "Turbo" is the host's own speed,
and the ROM cannot tell the difference.

## The Kestrel's map

| Address | What |
| --- | --- |
| `$0000`–`$00FF` | Zero page. The KERNAL owns `$00`–`$1F`; the socket ROM the rest |
| `$0100`–`$01FF` | The processor's stack |
| `$0200`–`$02FF` | The input line |
| `$0300`–`$06FF` | Kestrel DOS buffers: the directory, the volume header, one data sector |
| `$0700`–`$070F` | The keyboard buffer the frame interrupt fills |
| `$0710`–`$073F` | Workspace: the name a disk call is looking for, the SAVE counters, four `DEF FN` definitions |
| `$0740`–`$07FF` | The digits of a number on its way to the screen |
| `$0801`–`$7EFF` | Program, variables and free memory |
| `$7F00`–`$7F47` | BASIC's value stack: twelve levels of six bytes |
| `$7F50`–`$7FBF` | Seven `FOR` frames of sixteen bytes |
| `$7FC0`–`$7FFF` | Sixteen `GOSUB` frames of four bytes |
| `$8000`–`$83E7` | The tube: 1000 cells, row-major |
| `$83E8`–`$83FF` | Padding. Reads answer a space, writes are lost |
| `$8400`–`$87E7` | One attribute byte a cell: ink in the high nibble, paper in the low |
| `$8800`–`$88FF` | The I/O page |
| `$8900`–`$8FFF` | Nothing responds: reads are zero, writes are lost |
| `$9000`–`$9FFF` | A 4 KiB cartridge, if one is fitted |
| `$A000`–`$DFFF` | The language socket |
| `$E000`–`$FFFF` | The KERNAL, with the three vectors in the top six bytes |

The screen is 1000 cells but the row is 40, so row `r`, column `c` is
`$8000 + r*40 + c`. Scrolling copies rows 1–24 up over rows 0–23 and blanks the
last, then sets the scrolled flag in the I/O page.

## The Kestrel 16's map

The 65816 sees twenty-four address lines, so the machine is sixteen 64 KiB banks.
Bank zero keeps the shape the Kestrel had, which is what makes them one family.

| Bank | Address | What |
| --- | --- | --- |
| `$00` | `$0000`–`$00FF` | Zero page |
| `$00` | `$0100`–`$01FF` | The processor's stack |
| `$00` | `$0200`–`$07FF` | System RAM: keyboard buffer at `$0300`, input line at `$0400`, line buffer at `$0500`, the language's variables |
| `$00` | `$0800`–`$7EFF` | Free RAM; `MEMTOP` answers `$7F00` |
| `$00` | `$8000`–`$8F9F` | The tube: 4000 cells, row-major |
| `$00` | `$8FA0`–`$9F3F` | One attribute byte a cell |
| `$00` | `$A000`–`$A0FF` | The I/O page |
| `$00` | `$A100`–`$BFFF` | Nothing responds |
| `$00` | `$C000`–`$DFFF` | Cartridge ROM, 8 KiB |
| `$00` | `$E000`–`$FFFF` | The KERNAL, with the vectors in the top sixteen bytes |
| `$01` | `$0000`–`$FFFF` | The language ROM, whole |
| `$02`–`$03` | | RAM |
| `$04`–`$07` | | The cartridge's RAM, 256 KiB, when one is fitted |
| `$08`–`$0F` | | RAM |

A megabyte is all sixteen banks; what the table lists as ROM, cartridge and I/O
is what stands in front of it. The screen row is 80 cells, so row `r`, column `c`
is `$00:8000 + r*80 + c`.

## The I/O pages

A write is an output to a device and a read is an input from one; neither page is
storage. The offsets are the same idea on both machines, with different names for
the disk and tape registers.

| Offset | Kestrel | Kestrel 16 |
| --- | --- | --- |
| `$00` | Keyboard data: the next key, `0` when empty | the same |
| `$01` | Keyboard status: `$FF` while a key waits | bit 7 set while a key waits |
| `$02` | Write anything to clear the buffer | the same |
| `$03` / `$04` | Cursor column / row | the same |
| `$05` / `$06` | Ink / paper | the same |
| `$07` | Border | the same |
| `$08` | Sound: a write plays the note, a read answers `0` | the same |
| `$09` / `$0A` | Frame counter, low byte first | the same |
| `$0B` | `$FF` when a frame has passed; write `0` to clear | bit 7 set when a frame has passed |
| `$0C` / `$0D` | Disk sector number | the same |
| `$0E` | Disk command: `1` read, `2` write, `3` report the count | `1` read, `2` write, `3` format, `4` query |
| `$0F` | Disk status: `0` fine, `$80` no disk, `$81` bad sector, `$82` bad command | bit 7 done, bit 2 present, bit 1 error |
| `$10` / `$11` | Where in RAM the sector goes or comes from | a 24-bit address, low byte first |
| `$12` | The volume's sector count, low byte | the address's bank |
| `$13` | The volume's sector count, high byte | sectors to move, low byte |
| `$14` | A byte from the noise generator; reading advances it | sectors to move, high byte |
| `$15` | The last key the controller saw | the noise generator |
| `$16` | `$FF` once the screen has scrolled | the last key |
| `$17` | `1` while the cursor is lit | set when the screen scrolled |
| `$18` | Tape data | cursor on |
| `$19` | Tape status | tape data |
| `$1A` | Tape control | tape status |
| `$1B` | — | tape control |

The status bits that read `$FF` or `$80` on the frame flag, the scrolled flag and
the keyboard status are the machines' own difference: the 1982 machine answers
all-ones, the 1992 one sets bit 7.

A disk command runs inside the write that starts it. On the Kestrel 16 there is
no busy bit to see because there is nothing to wait for.

## Colour

**The Kestrel.** Sixteen codes, and the attribute byte holds two of them: ink in
the high nibble, paper in the low. There is no brightness bit — four bits is the
whole of the choice.

| Code | | Code | | Code | | Code | |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `0` | black | `4` | purple | `8` | orange | `12` | grey |
| `1` | white | `5` | green | `9` | brown | `13` | light green |
| `2` | red | `6` | blue | `10` | light red | `14` | light blue |
| `3` | cyan | `7` | yellow | `11` | dark grey | `15` | light grey |

**The Kestrel 16.** Sixteen codes packed the other way round: bits 0–2 are the
gun strengths and bit 3 is brightness.

| Code | | Code | | Code | | Code | |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `0` | black | `4` | green | `8` | dark grey | `12` | light green |
| `1` | blue | `5` | cyan | `9` | light blue | `13` | light cyan |
| `2` | red | `6` | yellow | `10` | light red | `14` | light yellow |
| `3` | magenta | `7` | light grey | `11` | light magenta | `15` | white |

Which red, and how bright, is the tube's business: the window draws a muted set
that suits each machine's decade.

## The bus, and the order it answers in

A read or a write is decoded in one order, and the order matters where regions
abut. `Machine::read` and `Machine::write` in `src/kestrel/machine.cpp` are the
whole of it.

**The Kestrel**, in order:

1. RAM, `$0000` up to the profile's RAM size;
2. the tube's cells;
3. the padding between cells and attributes, which reads as a space;
4. the attributes;
5. the I/O page;
6. the cartridge, if one is fitted — otherwise zero;
7. the language socket;
8. the KERNAL, from `$E000` to the top;
9. anything else reads zero and swallows writes.

**The Kestrel 16**, bank zero: the KERNAL first (it is at the top of the bank and
wins over RAM), then the cartridge, the screen, the attributes, the I/O page, and
RAM for everything else. Bank `$01` is the language ROM; banks `$04`–`$07` are
the cartridge when it is fitted; the rest are RAM.

A ROM image mounted for the wrong machine, or of the wrong size, is refused
rather than padded: the KERNAL must be exactly 8 KiB, the language exactly 16 KiB
on the Kestrel and 64 KiB on the Kestrel 16.

## Interrupts, the frame and the clock

Fifty times a second the machine counts a frame: the counter at `$09`/`$0A` goes
up by one, low byte first, and the frame flag at `$0B` is set. That is the whole
of the machine's part.

The interrupt line is high while **either** a key is waiting **or** the frame flag
is set, and it is the ROM's handler that clears the flag and empties the keyboard
controller. A handler that forgot to drain the controller would leave the line
high for ever and the machine would stop taking keys — which is why the flag is
cleared by a write and the keys are read, not merely counted.

**BREAK** is a non-maskable interrupt, and it is a warm start: the KERNAL keeps
memory, abandons what was running, and puts the prompt back. Both KERNALs clear
the interrupt-disable flag on the way in, so a machine that has just been
interrupted can read its keyboard again.

A frame is 20 000 cycles on the Kestrel and 160 000 on the Kestrel 16: 1 MHz and
8 MHz divided by 50. The command line's `--frames N` counts these; `--speed`
chooses how fast the host runs them.

## The keyboard

Both machines have a controller: a queue the ROM drains through the I/O page. A
key is queued by the host with `Machine::type`, read with a read of `$00`, and its
arrival announced by `$01`. The buffers hold sixteen keys on the Kestrel and
thirty-two on the Kestrel 16, which is a real limit: a pasted line can overflow
it and lose the Return.

The keys that produce no character have numbers of their own:

| Key | Code |
| --- | --- |
| Return | 13 |
| Backspace | 8 |
| Escape | 27 |
| Left, Right, Up, Down | 28, 29, 30, 31 |
| Delete | 127 |

## The disk

A disk is sectors and nothing more. The emulator's `Disk` has no filesystem: each
machine's DOS is machine code in its ROM and works through the sector registers,
which is why a program that saves a file exercises the ROM rather than the host.

| | Kestrel | Kestrel 16 |
| --- | --- | --- |
| Sectors | 512 | 4096 |
| Bytes a sector | 256 | 512 |
| Volume size | 131 072 bytes | 2 097 152 bytes |
| Image extension | `.dsk` | `.dsk` |
| Fresh volume's name | `KESWARP` | — |

A disk image of any other size is refused when mounted.

### Kestrel DOS 1.0, on the disk

| Where | What |
| --- | --- |
| Sector 0 | The volume header: signature, volume name, sector count |
| Sectors 1–2 | The directory: 32 entries of 16 bytes — name, first sector, length, type |
| Sector 3 on | Files: 252 bytes of data, a pointer to the next sector, and how much of this sector is used |

The controller's status byte is the error report: `0` fine, `$80` no disk, `$81`
bad sector, `$82` bad command.

## The cassette

A tape is a byte stream with a play head, which is all a tape ever was to the
machine. The block format belongs to the KERNAL — on the Kestrel, the monitor
ROM's `S` and `L`, with a four-byte header of load address and length.

| Tape status | Bit |
| --- | --- |
| present | 7 |
| playing | 6 |
| recording | 5 |
| more to read | 4 |

| Tape control | Command |
| --- | --- |
| 1 | play |
| 2 | record |
| 3 | stop |
| 4 | rewind |

## The ROMs

| ROM | Machine | Size | Origin | Built from |
| --- | --- | --- | --- | --- |
| KERNAL | both | 8192 | `$E000` / `$00:E000` | `roms/kestrel/kernal.s`, `roms/kestrel16/kernal.s` |
| BASIC | Kestrel | 16384 | `$A000` | `roms/kestrel/basic.s` |
| Monitor | Kestrel | 16384 | `$A000` | `roms/kestrel/monitor.s` |
| BASIC | Kestrel 16 | 65536 | `$01:0000` | `roms/kestrel16/basic.s` |

Each assembler writes a header of bytes into `build/generated/` at build time;
the machine is given those bytes and nothing else. The Kestrel's socket takes one
of `basic` or `monitor` — `kestrel --rom monitor`, or **Machine ▸ ROM socket** in
the window — and changing it cold-boots the machine, because the reset vector in
the new image is where the processor starts.

The reset vector is `$FFFC` on the Kestrel. On the Kestrel 16 the processor reads
its vectors from the top of bank zero, and leaves reset in emulation mode; the
KERNAL's first few instructions leave emulation behind.

## The processors

Both are used whole. Neither is interchangeable with the other: the machine
steps only the one its model has.

### The 6502

The full instruction set, cycle-counted, with the undocumented opcodes counted
rather than executed. Decimal mode is implemented for `ADC` and `SBC`, and the
flags follow the chip's rules — including `V` and `N` from the binary result while
the accumulator holds the decimal one.

### The 65C816

Emulation and native modes, eight- and sixteen-bit accumulators and indexes, the
direct page, the data and program banks, and the long addressing modes. Three
rules are worth stating because they are easy to get wrong and the machine has
been wrong about them before:

- **`JMP` stays in the program bank.** The sixteen-bit address does not carry a
  bank, whatever the data bank holds. `JML` is the long form, and its bank is part
  of the address.
- **An immediate is as wide as the accumulator, and the assembler cannot see the
  flags.** Between `SEP #$20` and its `REP #$20`, a word immediate says `.W`; a
  byte immediate says nothing.
- **`BRA` has no inverse.** A branch that cannot reach becomes a `JMP`, not an
  inverse branch over one.

`illegalOpcodes()` and `lastIllegalOpcode()` report what a machine fetched that
it could not execute, which is how a runaway ROM is caught in a test.

## Kestrel BASIC's numbers

The 1982 machine's arithmetic is 6502 code in the socket ROM, in
`roms/kestrel/fp.s`, and it is five bytes a number:

| Byte | Holds |
| --- | --- |
| 0 | The exponent, biased by 128; an exponent of zero means the number is zero |
| 1–4 | The mantissa, most significant byte first, with the sign in bit 7 of the first |

The value is `(mantissa / 2^32) * 2^(exponent - 128)`, normalised so the
mantissa's leading bit is set — that is, `0.5 <= |m| < 1`. Nine significant
decimal digits fall out of that, and a range from about `2.9E-39` to `1.7E38`.

Both ends are outcomes, and neither is silent:

- **above the top** — the exponent would have to pass 255 — the machine reports
  `?OVERFLOW`. That is a `JMP` to the error printer from the arithmetic itself;
  the first attempt at this left the exponent to wrap, which turned `1E39` into
  `8.6E-39` and `1E38+1E38` into zero;
- **below the bottom** — the exponent would reach zero, which is the code for
  zero — the result is zero, quietly. `1E-39`, `1E-38/100` and `EXP(-90)` are all
  `0`.

The Kestrel 16 has no floating point at all: sixteen-bit integers, which wrap.

## What the machines do not have

- **No floating point hardware.** Kestrel BASIC's arithmetic is 6502 code in the
  socket ROM; the Kestrel 16's is sixteen-bit integer code in its bank.
- **No video hardware beyond a matrix and a colour byte a cell.** No sprites, no
  scrolling registers, no raster interrupt: the scroll is the KERNAL's, in code.
- **No audio hardware beyond a latch.** A write is a note; the window decides what
  a note sounds like.
- **No timers but the frame.** The frame counter is the clock a ROM has.
- **No interrupts but the frame, the keyboard and BREAK**, and on the Kestrel the
  keyboard's own line is high until the handler has drained it.
