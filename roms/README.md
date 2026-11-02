# The ROMs

A machine of this kind was a box with a socket in it. The KERNAL was soldered in
and never changed; the language went in a socket, on a chip, in a box, with a
manual, and it could be bought separately. That is why there are four of them
here rather than one, and why each is a complete source of its own that the
machine assembles when it starts.

```
roms/
  kestrel/            1982: a 16 KiB socket at $A000
    kestrel.inc       the map and the KERNAL's jump table, for every ROM
    bz.inc            BASIC's zero page, for BASIC's own pieces
    kernal.s          the KERNAL: ROM, not a socket ROM
    basic.s           Kestrel BASIC V2.0        }
    monitor.s         Kestrel Monitor V1.0      }  a language source, and its
    pilot.s           Kestrel PILOT V1.0        }  pieces as .inc files
    diagnostics.s     Kestrel Diagnostics V1.0  }
    def.inc ary.inc str.inc func.inc fp.inc num.inc eval.inc dos.inc
  kestrel16/          1992: a 64 KiB language bank
    kestrel16.inc     the map, the KERNAL and the long-call table
    prog.inc          the language bank's own zero page and workspace
    kernal.s          the KERNAL
    basic.s           Kestrel 16 BASIC 2.0
```

A `.s` file is a ROM: it is assembled to the whole of the socket and starts the
machine. A `.inc` file is a piece of one and is never assembled on its own —
which is why the socket's catalogue is exactly the list of `.s` files.

## The catalogue

### Kestrel, 1982 — 16 KiB in the socket at `$A000`

| ROM | What it is | What it does |
| --- | --- | --- |
| [basic](kestrel/basic.s) | Kestrel BASIC V2.0 | The language the machine was switched on with: numbered lines, floating point to nine digits, strings and arrays, `DATA`, `DEF FN`, and the disk commands |
| [monitor](kestrel/monitor.s) | Kestrel Monitor V1.0 | The processor's view: examine and change memory, look at the registers, save a block to tape, load one back |
| [pilot](kestrel/pilot.s) | Kestrel PILOT V1.0 | The teaching language sold to schools: a lesson is numbered lines of `T:` text, `A:` answer, `M:` match and `Y:`/`N:`/`J:` jumps |
| [diagnostics](kestrel/diagnostics.s) | Kestrel Diagnostics V1.0 | What a dealer fitted to a machine that came back faulty: a memory test that names the address that failed, a tube test, a note to listen for, and every key with its code |

### Kestrel 16, 1992 — 64 KiB in the language bank

| ROM | What it is | What it does |
| --- | --- | --- |
| [basic](kestrel16/basic.s) | Kestrel 16 BASIC 2.0 | The language in bank one: 80 × 50 tube, 16 colours, integer arithmetic, the disk and the tape |

The Kestrel 16 has no socket for a language: its language is one whole bank, and
the machine's expansions came on cartridges instead. See
[the cartridge catalogue](../cartridges/README.md).

## Choosing one

```sh
./start.sh kestrel --rom pilot           # or, from rust/:
cargo run --release --bin kestrel -- --rom diagnostics --frames 400
cargo run --release --bin kestrel -- --languages       # what there is
```

`--rom` takes a name from the catalogue above. The machine is reset to start it,
which is what changing a chip means. In the window emulator it is the **ROM
socket** group on the **Machine** page of the settings sheet; in `kestrel_emu`
it is `/rom pilot`, and `/rom` on its own says which one is fitted.

## Kestrel PILOT V1.0

PILOT is a language for teaching, so a lesson is a list of numbered lines and
each line does one thing. A line typed without a number is obeyed at once, which
is how a teacher tries a line out before putting it in.

| Statement | What it does |
| --- | --- |
| `T: text` | Type the text, then a new line. `$A` prints answer `A`; `$$` prints a dollar sign |
| `A: letter` | Accept an answer into that variable, up to fifteen characters |
| `M: text` | Does the answer just accepted match this text? `*` matches anything |
| `Y: label` | Go to the label when it matched |
| `N: label` | Go to the label when it did not |
| `J: label` | Go to the label |
| `E:` | End the program |
| `R: text` | A remark |
| `:label` | A label, which is where a jump goes |

The commands are `RUN`, `LIST` and `NEW`; a number on its own deletes that line;
and everything — statements, labels and matching — is read without regard to
case. A lesson, as it would be typed:

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
RUN
```

The program is kept as records in RAM at `$1000` and up — a line number, the
text, and a return — so a line longer than the one it replaces moves everything
after it, and the program is one buffer with no length to keep in step. Answers
live at `$0900`, sixteen bytes each, and are cleared when the machine is
switched on.

## Kestrel Diagnostics V1.0

The service ROM. It runs its tests on the way up and prints one line for each:

```
*** KESTREL DIAGNOSTICS V1.0 ***

RAM  $0800-$7FFF  OK
TUBE 40 X 25      OK
SOUND             LISTEN FOR THE NOTE
KEYBOARD          PRESS ANY KEY
                  BREAK TESTS AGAIN
KEY $41 A
```

- **RAM** is filled with `$55` and then `$AA` and read back both times, from
  `$0800` — the first address that is not the KERNAL's own workspace — to
  `$7FFF`. A byte that does not come back is printed as an address, which is
  what the shop wrote on the job sheet.
- **The tube** is put away in RAM, filled with a pattern and read back through
  the memory programs use, and then put back, so the report above it is still
  there to read. Characters and colours are tested as two separate blocks
  because the gap between them answers to nobody.
- **Sound** has no read-back on this machine, so the test is a note and a wait
  rather than a pass.
- **The keyboard** prints every key as a code and again as a character, for as
  long as the machine is left on. `BREAK` starts the tests again.

The whole run takes a few seconds on a one-megahertz machine, which is what a
memory test of thirty kilobytes costs:

```sh
cargo run --release --bin kestrel -- --rom diagnostics --frames 400
```

## Writing one

A ROM in the socket answers two addresses: the cold start, which the KERNAL's
reset jumps to, and the warm start, which `BREAK` jumps to. Both are named in
`kestrel.inc`, and the rest is an ordinary 6502 program with the KERNAL's jump
table to call:

```asm
        .include "kestrel.inc"

        .org SOCKET

COLD_START_ENTRY:
        JMP MY_START
WARM_START_ENTRY:
        JMP MY_START

MY_START:
        ...
```

The KERNAL owns the zero page from `$00` to `$1F`; a ROM in the socket may use
`$20` upwards, and every call to the KERNAL may use `ZP_TMP`, `ZP_PTR` and the
rest, so nothing that must survive a `CHROUT` belongs there. `PILOT` and
`Diagnostics` both keep their state at `$20`–`$3F` and their data above `$0800`.

An image of the wrong size is refused rather than half-fitted, and the source is
assembled to the whole socket, so a small ROM still fills its 16 KiB the way the
chip in the box did.
