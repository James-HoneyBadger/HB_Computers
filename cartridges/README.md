# Cartridges

A cartridge is the part of these machines that was never in a ROM: a program on
a board, sold in a box, that took the machine over when it was switched on. The
sources here are assembled into the images the cartridge slot answers to, exactly
as `roms/` is assembled into the KERNAL and the language.

```
cartridges/
  kestrel/            4 KiB at $9000, for the 1982 machine
    cart.inc          the map, and the header a cartridge carries
    monitor.s         Kestrel Machine Monitor
    tutor.s           Kestrel Tutor
  kestrel16/          128 KiB, for the 1992 machine
    cart.inc          the same, with the 1992 addresses
    colours.s         Kestrel 16 Colour Demonstration
```

## The catalogue

### Kestrel, 1982 — 4 KiB cartridges

| Cartridge | What it is | What it does |
| --- | --- | --- |
| [monitor](kestrel/monitor.s) | Kestrel Machine Monitor | An address, the bytes at it, and a way to change them: `D 8000` dumps sixty-four bytes, `M 8000 42 43` puts two there, `J E000` goes there, `B` hands the machine back to BASIC |
| [tutor](kestrel/tutor.s) | Kestrel Tutor | Sets the multiplication tables, marks the answer and keeps a score, one question to a line |

### Kestrel 16, 1992 — 128 KiB cartridges

| Cartridge | What it is | What it does |
| --- | --- | --- |
| [colours](kestrel16/colours.s) | Kestrel 16 Colour Demonstration | Fills the tube with a texture and a colour for every cell, and leaves it on the screen: the cartridge a shop put in the window |

## How a cartridge is started

The KERNAL's reset looks at the bottom of the slot, once, on the way up. If the
first four bytes are `KSTL` it jumps to the address in the word that follows
them; if they are not, or no cartridge is fitted, the machine starts its
language as usual.

| | Kestrel | Kestrel 16 |
| --- | --- | --- |
| Slot | `$9000`–`$9FFF` | `$00:C000`–`$00:DFFF`, and the whole image across banks `$04`–`$07` |
| Image | 4096 bytes | 131 072 bytes |
| Header | `KSTL`, then the start address, low byte first | the same, an address in bank zero |
| Reset's scan | `CART_SCAN` in `roms/kestrel/kernal.s` | `CART_SCAN` in `roms/kestrel16/kernal.s` |

A cartridge owns the machine once it is jumped to. It may use the KERNAL's jump
table like any other program, and `JMP COLD_START` on the 1982 machine (or
`JML BASIC` on the 1992 one) gives the machine back to its language.

## Running one

```sh
./start.sh kestrel --cart monitor          # or, from rust/:
cargo run --release --bin kestrel -- --cart tutor
cargo run --release --bin kestrel -- --cartridges     # what there is
```

`--cart` takes either a name from this folder, which is assembled from its source
first, or the path of an image already assembled. The window emulator fits one
from **Cartridge** in its settings sheet, or from the **Cartridge** menu.

## Writing one

A cartridge source includes `cart.inc` from its own folder, which brings in the
machine's map and defines where a cartridge's own variables live:

```asm
        .include "cart.inc"

        .org CART

MY_SIG:
        .text "KSTL"            ; the four letters the reset looks for
MY_TABLE:
        .word MY_START          ; and where to go when it finds them

MY_START:
        ...
```

Two rules cost more time than anything else if they are forgotten:

- **Keep state in RAM, not in the zero page.** Every call to the KERNAL may use
  every cell of the zero page it likes; `CHROUT` loads the screen pointer into
  `ZP_PTR` for each character. `cart.inc` names a block at `$0800`, which is the
  program area of the language the cartridge replaces.
- **Fill the image.** The slot takes the whole of a cartridge and nothing else,
  so an image of the wrong size is refused rather than half-fitted. The sources
  are assembled to the slot's size, with the rest of the image left as `$FF`.

An image made by another assembler is a cartridge all the same, as long as it is
exactly the size of the slot and begins with the four letters.
