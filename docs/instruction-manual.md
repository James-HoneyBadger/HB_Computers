# Instruction Manual

Two home computers, and the emulator that is both of them. This book covers what
the machines are, how to get them running, which keys do what, and what to do
when the screen does not say what you expected.

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
| Language | 16 KiB socket ROM: Kestrel BASIC V2.0, PILOT, Diagnostics or the monitor | 64 KiB bank: Kestrel 16 BASIC 2.0 |
| KERNAL | 8 KiB at `$E000` | 8 KiB at `$00:E000` |

Neither machine is simulated above the hardware: the host supplies a processor, a
bus, a tube, a keyboard and a disk, and everything on the glass is machine code
fetched from a ROM image and executed.

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
fails, and everything it builds lands in `rust/target/release`.

**In a virtual machine.** The tree carries a `Vagrantfile` that makes an Ubuntu
22.04 build machine, installs the tools, and builds and tests everything:

```bash
vagrant up     # provision, build, run every suite
vagrant ssh    # the tree is at /vagrant
```

The window is built there but not shown: a guest has no display unless it is
given one, and the suites do not need it.

## Switching on

**In the window**

```bash
./start.sh                    # the Kestrel
./start.sh kestrel16          # the Kestrel 16
./start.sh window --model kestrel       # either, with arguments passed through
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

The banner is machine code, not a host splash: on the Kestrel the KERNAL's reset
routine prints it before the language ROM gets control, and on the Kestrel 16 it
is the KERNAL's first act, before it hands over to bank one.

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
| `Pause`, `Shift+F1` | a break line, which is a warm start |

Letters arrive as typed, upper or lower. The Kestrel's BASIC is case-blind about
keywords, so `print 2+2` and `PRINT 2+2` are the same statement, and a program
typed in lower case lists back the way it was typed.

`Ctrl+Q` closes the window.

## The window

The window is the machine on a bench: the tube in the middle, a menu bar above
it, and a status line below it that says what is fitted — the clock, and a lamp
each for the disk, the tape and the cartridge.

| Menu | What it holds |
| --- | --- |
| **Machine** | reset, BREAK, pause, and a fresh build of the ROMs |
| **Media ▸ Floppy disk / Tape / Cartridge** | mount, save, format and eject, without opening the settings |
| **Settings ▸ Settings…** | the settings window, on `F12` or `Ctrl+,` |
| **Settings ▸ Save / Load configuration…** | the switches as a small text file |
| **View** | zoom, the border colour, the cursor, and a screenshot |
| **Help ▸ Commands** | the command list for the machine that is running |

The window is shaped after WinUAE, which is where an emulator's front end has
settled: the settings are a **sheet of their own**, opened over the machine, and
the machine stands still while its switches are out.

| Key | Does |
| --- | --- |
| `F12`, `Ctrl+,` | open and close the settings |
| `Esc` | close the settings, putting everything back |
| `Ctrl+R` | reset the machine |
| `Ctrl+P` | pause and resume |
| `Ctrl+Q` | quit |

### The settings sheet

A tree of properties down the left, the chosen page on the right, and `OK`,
`Cancel` and `Reset` along the foot. A page is a set of groups: labelled boxes,
one for each thing being set.

| Page | What it sets |
| --- | --- |
| **Quickstart** | which machine, with its own clock, in one click |
| **Machine** | what the machine is made of, and which ROM is in the language socket (on the Kestrel 16, in its language bank) |
| **CPU & speed** | 1, 2, 4 or 8 MHz, or as fast as this computer |
| **ROM** | where the ROM images are read from, and a rebuild |
| **Cartridge** | the expansion cartridge, its size and where it sits |
| **Floppy disk** | the volume, its name and size, and mount, save, format, eject |
| **Tape** | the cassette's state and length, and insert, save, rewind, eject |
| **Display** | zoom, the border colour, the cursor, and the sixteen colours |
| **Sound** | whether the host makes the beep the sound latch asks for |
| **About** | the version, the keys, and what a dropped file becomes |

`OK` applies the page — a different machine is powered on, which loses whatever
was in memory. `Cancel` puts every switch back as it was. `Media` actions take
effect at once, because a disk is hardware and not a setting.

Speed is a convenience, not a disguise: the processor is the same one at any
setting, and a program that depends on timing only works at the machine's own
clock.

### Configurations

**Settings ▸ Save configuration…** writes the switches as `name=value` lines —
the machine, the speed, the zoom, the border, the cursor and the sound — and
**Load configuration…** reads them back. A configuration says what the bench is
set to; what is on the disk and the tape is the media's business.

## Media

**Disks.** A disk image is the volume's sectors and nothing else — `131072`
bytes for the Kestrel (512 × 256), `2097152` for the Kestrel 16 (4096 × 512).
An image of any other size is refused, because half a disk is a worse lie than
no disk. The 1982 machine's `FORMAT` names a fresh volume `KESWARP`; the 1992
one leaves the name blank until a way to pass one exists. Either can be formatted
again from **Media ▸ Floppy disk ▸ Format disk**, or from the disk page of the
settings.

**Tape.** The cassette is a byte stream with a play head. The Kestrel's BASIC has
no tape words: the monitor ROM's `S` and `L` write and read blocks of memory.
The Kestrel 16's `CSAVE` and `CLOAD` move the program store as named blocks with
a length and a check byte.

**Cartridge.** A 4 KiB image at `$9000` on the Kestrel. On the Kestrel 16 the
image is 128 KiB, and the machine shows it twice: through an 8 KiB window at
`$00:C000`–`$DFFF`, and across banks `$04`–`$07`.

**The ROM socket.** The Kestrel's language socket takes one 16 KiB image at a
time, and which one is in it is what the machine is: Kestrel BASIC V2.0, Kestrel
PILOT, Kestrel Diagnostics, or the monitor ROM. The machine cold-boots when it
changes, so it is a different machine afterwards. `roms/README.md` is the
catalogue; `--rom pilot` and the **ROM socket** group in the window's settings
sheet are how one is chosen.

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

Note that nothing was printed after any of the numbered lines: `READY.` appeared
once, when `RUN` had finished. The prompt is what says a *command* has finished,
and a numbered line is not a command — it goes into the program and the machine
waits, silently, for the next line. Both machines behave this way, so a listing
typed in at the keyboard is a column of lines with no prompt between them.

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

The arithmetic is sixteen-bit integers, so a division throws the fraction away
and a product past 32767 comes back negative. A program is stored the same way
as on the 1982 machine — a numbered line is kept, `RUN` runs it — and the
language has `IF`/`THEN`/`ELSE`, `FOR`/`NEXT`, `GOSUB`/`RETURN`, arrays, and
functions of the program's own:

```basic
10 FOR I=1 TO 3
20 PRINT I;" SQUARED IS ";I*I
30 NEXT I
RUN
```

```
1 SQUARED IS 1
2 SQUARED IS 4
3 SQUARED IS 9
READY.
```

What it does not have is a keyboard input statement, random numbers, `INT` or
`SQR`: the language is smaller than the 1982 one, which is why the samples for
this machine use the noise register and integer arithmetic in their place
([the User Guide](users-guide.md#kestrel-16-basic) lists the differences).
`CLS` clears the screen and prints the banner again.

---

## The samples

| Directory | What is in it |
| --- | --- |
| [samples/kestrel](../samples/kestrel) | twenty-two BASIC programs: a sieve, Fibonacci, factorials, strings, arrays, subroutines, `POKE`, colour and sound, and then a game, a utility and a business program apiece |
| [samples/kestrel16](../samples/kestrel16) | twenty programs: sixteen-bit arithmetic, branches and loops, strings, arrays, functions of the program's own, and the same spread of games, utilities and business programs |

Each sample has a `.expected` file beside it holding the lines the machine must
put on the screen. The samples suite runs every one of them, so a sample
that stops working is a failing suite rather than a stale page.

Run one at a time:

```bash
./start.sh cli --model kestrel16 --file samples/kestrel16/03-division.bas
```

## The cartridges

A cartridge is software on a board, sold in a box: it goes in the slot and the
machine starts it instead of its language. [cartridges/](../cartridges) holds the
ones this repository ships, as sources that are assembled when they are fitted.

| Cartridge | Machine | What it is |
| --- | --- | --- |
| `monitor` | Kestrel | Kestrel Machine Monitor: `D` dumps memory, `M` changes it, `J` runs it, `B` goes back to BASIC |
| `tutor` | Kestrel | Kestrel Tutor: the multiplication tables, marked, with a score |
| `colours` | Kestrel 16 | Kestrel 16 Colour Demonstration: the tube filled with texture and colour |

```bash
./start.sh cli --cart monitor          # the monitor, in a Kestrel that was switched on with it
./start.sh kestrel --cart monitor      # the same in the window
```

The window fits one from **Cartridge** in its settings sheet, or from the
**Cartridge** menu, and ejects it there too: fitting a cartridge starts the
machine again, which is what a cartridge always needed.

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
