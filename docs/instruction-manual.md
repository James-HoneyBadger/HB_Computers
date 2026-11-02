# Instruction Manual

Two home computers, and the emulator that is both of them. This book is the one
that comes in the box: what the machines are, how to get them running, which
keys do what, and what to do when the screen does not say what you expected.

The other three books are for after this one:

| Book | For |
| --- | --- |
| [User Guide](users-guide.md) | using the software: BASIC, the monitor, the disk |
| [Technical Reference](technical-reference.md) | the maps, the registers, the formats |
| [Programmer's Guide](programmers-guide.md) | writing machine code and ROMs, and changing the emulator |

---

## What is in the box

| | Kestrel (1982) | Kestrel 16 (1992) |
| --- | --- | --- |
| Processor | MOS 6502 at 1 MHz | WDC 65C816 at 8 MHz |
| Memory | 32 KiB of RAM behind a banner that says `64K` | 1 MiB of banked RAM |
| Screen | 40 columns by 25 rows | 80 columns by 50 rows |
| Colour | sixteen inks and sixteen papers | sixteen inks and sixteen papers |
| Sound | one latch byte | one latch byte |
| Disk | 512 sectors of 256 bytes | 4096 sectors of 512 bytes |
| Cassette | a byte stream | a byte stream |
| Language | 16 KiB socket ROM: Kestrel BASIC V2.0, or the monitor | 64 KiB bank: Kestrel 16 BASIC 2.0 |
| KERNAL | 8 KiB at `$E000` | 8 KiB at `$00:E000` |

Neither machine is a picture of a machine. The host supplies a processor, a bus,
a tube, a keyboard and a disk; everything on the glass is machine code
assembled into a ROM image and executed.

## What you need

- a Rust toolchain (`cargo` and `rustc`);
- a display, if you want the window. Without one the machines, the two
  assemblers, the command line, the emulator and the tests all still work;
- a terminal.

## Unpacking

```bash
cd rust
cargo build --release
```

Or, with the start script in the root of the tree:

```bash
./start.sh build          # build everything that is not built yet
./start.sh                # build, then the Kestrel's window
./start.sh kestrel16      # build, then the Kestrel 16's window
./start.sh test           # build, then every suite
./start.sh cli --model kestrel --type "PRINT 2+2"
```

`./start.sh` keeps the machine's output clean: the build is silent unless it
fails, and its log is in `build/start.log`.

**In a virtual machine.** The tree carries a `Vagrantfile` that makes an Ubuntu
22.04 build machine, installs the tools, and builds and tests everything:

```bash
vagrant up     # provision, build, run the eight suites
vagrant ssh    # the tree is at /vagrant
```

The window is built there but not shown: a guest has no display unless it is
given one, and the suites do not need it.

## Switching on

**In the window**

```bash
./start.sh                    # the Kestrel
./start.sh --model kestrel16  # the Kestrel 16
```

**Interactively** — either machine, chosen at the prompt, with a session around
it; `/model` changes machine without leaving:

```bash
./start.sh emu                 # asks: 1 the Kestrel, 2 the Kestrel 16
./start.sh emu --model kestrel16
```

**On a shell** — the same machines with no window, which is how the tests and
the samples are run:

```bash
./start.sh cli --model kestrel --type "PRINT 2+2"
./start.sh cli --model kestrel16 --type "PRINT 2+2"
```

The banner is not decoration. On the Kestrel it is printed by the KERNAL's
reset routine before the language ROM gets control; on the Kestrel 16 it is the
KERNAL's first act, before it hands over to bank one. If you see a banner and a
prompt, a processor really did fetch those instructions.

---

## The keyboard

| Key | Sends |
| --- | --- |
| A–Z, 0–9, punctuation | its ASCII code |
| Return | 13 |
| Backspace | 8 |
| Escape | 27 |
| Arrows | 28–31 |
| Delete | 127 |
| `Pause` | the BREAK line, a warm start |

Letters arrive as typed, upper or lower. The Kestrel's BASIC is case-blind about
keywords, so `print 2+2` and `PRINT 2+2` are the same statement, and a program
typed in lower case lists back the way it was typed.

`Ctrl+Q` closes the window, which is the polite way to leave a machine on.

## The window

| Menu | What it holds |
| --- | --- |
| **Machine ▸ Speed** | 1, 2, 4 or 8 MHz, or as fast as this computer. The machines come up at their own clock: 1 MHz for the Kestrel, 8 MHz for the Kestrel 16 |
| **Machine ▸ ROM socket** | (Kestrel only) which ROM is in the language socket; changing it cold-boots the machine |
| **Machine ▸ Cartridge** | fit an expansion image — 4 KiB on the Kestrel, 8 KiB on the Kestrel 16 — or eject it |
| **Machine ▸ BREAK** | the warm start, on the `Pause` key |
| **Machine ▸ Sound** | whether the host makes the beep the sound latch asks for |
| **Media ▸ Mount disk image…** | a `.dsk` volume |
| **Media ▸ Save disk image…** | write the current volume back to a file |
| **Media ▸ Format disk** | a blank volume, named `KESWARP` |
| **Media ▸ Insert tape… / Save tape… / Eject tape** | cassette images |
| **Help ▸ Commands** | the command list, the same one `HELP` prints |

Speed is a convenience, not a disguise: the processor is the same one at any
setting, and a program that depends on timing only works at the machine's own
clock.

## Media

**Disks.** A disk image is the volume's sectors and nothing else — `131072`
bytes for the Kestrel (512 × 256), `2097152` for the Kestrel 16 (4096 × 512).
An image of any other size is refused, because half a disk is a worse lie than
no disk. A freshly formatted volume is named `KESWARP` on the Kestrel; BASIC has
no `FORMAT`, so the disk arrives formatted and is formatted again from **Media ▸
Format disk**.

**Tape.** The cassette is a byte stream with a play head. The Kestrel's BASIC has
no tape words: the monitor ROM's `S` and `L` are what write and read it. The
Kestrel 16 has the registers and no block format yet.

**Cartridge.** A 4 KiB image fitted at `$9000` on the Kestrel, 8 KiB at `$00:C000`
on the Kestrel 16 (with four banks of cartridge RAM above it).

**The ROM socket.** The Kestrel's language socket takes one 16 KiB image at a
time: Kestrel BASIC, or the monitor ROM. Which one is in it changes the machine,
so the window cold-boots when it changes.

---

## Your first session: the Kestrel

Switch on and type:

```basic
PRINT 2+2
```

```
4
READY.
```

Now a program. Type each line and press Return; a line that starts with a number
is stored, and a line without one is run at once:

```basic
10 FOR I=1 TO 5
20 PRINT "THE KESTREL SAYS";I
30 NEXT I
RUN
```

```
THE KESTREL SAYS1
THE KESTREL SAYS2
THE KESTREL SAYS3
THE KESTREL SAYS4
THE KESTREL SAYS5
READY.
```

`LIST` shows what is stored, `NEW` throws it away, and `BREAK` abandons a program
that is running without losing it.

Try a program from [samples/kestrel](../samples/kestrel), or run one from a
shell:

```bash
./start.sh cli --model kestrel --file samples/kestrel/02-sieve.bas --type RUN
```

## Your first session: the Kestrel 16

```basic
PRINT 2+2
PRINT 1000*20
PRINT -7/2
```

```
4
20000
-3
```

The 1992 machine's language is smaller on purpose: no line numbers, no stored
program, no `IF`, no `FOR`. What it has is sixteen-bit arithmetic, a wide screen
and the beginnings of a machine that a decade of software could have grown on.
`CLS` clears the screen and prints the banner again.

---

## The samples

| Directory | What is in it |
| --- | --- |
| [samples/kestrel](../samples/kestrel) | twelve BASIC programs: a sieve, Fibonacci, factorials, strings, arrays, subroutines, `POKE`, colour and sound |
| [samples/kestrel16](../samples/kestrel16) | six sets of direct statements, which is what the 1992 language takes today |

Each sample has a `.expected` file beside it holding the lines the machine must
put on the screen. the samples suite runs every one of them, so a sample
that stops working is a failing suite rather than a stale page.

Run one at a time:

```bash
./start.sh cli --model kestrel16 --file samples/kestrel16/03-division.bas
```

## If something goes wrong

| What you see | What it means |
| --- | --- |
| A key does nothing | The keyboard controller holds sixteen keys and is emptied fifty times a second. Typing faster than the emulated machine reads loses keys, exactly as it would have in 1982. The command line paces itself; a window pasting text may not |
| Two lines joined together | The same thing: the Return went missing with the last key. Press Return |
| `?SYNTAX ERROR` | The line is not a statement the language has, or something followed a statement that was not a `:` |
| `?OUT OF MEMORY` | The program, its variables and its strings no longer fit between `$0801` and the top of the free memory |
| The machine beeps and ignores you | The sound latch is the beep; the beep is the whole of the audio hardware |
| Nothing on the screen but a banner | The language ROM did not get control, or the program finished and the screen was cleared. `BREAK` puts the prompt back |
| A disk will not mount | The image is not exactly the machine's volume size |

## Care and feeding

- **BREAK** is a non-maskable interrupt, not a reset: the program stays in
  memory, and on the Kestrel `LIST` and `RUN` still work afterwards.
- **Changing the ROM socket** cold-boots the Kestrel, which clears memory.
- **Saving a disk** writes the whole volume, including its free map, so an image
  file is a photograph of a disk rather than a list of files.
- **Leaving the window open** costs nothing but a frame interrupt fifty times a
  second; the machines have no idle state to enter.

---

## Where to go next

- to use the software — [the User Guide](users-guide.md);
- to build for the machines — [the Technical Reference](technical-reference.md);
- to write ROM code or change the emulator — [the Programmer's Guide](programmers-guide.md);
- the machines' own books, in full: [the Kestrel](kestrel/README.md) and
  [the Kestrel 16](kestrel16/README.md).
