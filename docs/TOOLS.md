# The tools

Two assemblers that build the ROMs, one command line that boots the machines, and
the build that ties them together.

---

## `kestrel_asm` — the 6502 assembler

Assembles the Kestrel's ROMs. It is a 6502 assembler with the directives a ROM
needs and nothing else.

```bash
cd roms/kestrel                 # .include is resolved from the working directory
kestrel_asm basic.s --name basic --origin $A000 --size 16384 \
             --bin ../../build/basic.bin --listing basic.lst --symbols basic.sym
```

The origin may be written in hex with `$`, in hex with `0x`, or in decimal, and
the same goes for every number in the source.

| Option | Meaning |
| --- | --- |
| `--name` | What the image is called in the symbol table and the header comment |
| `--origin` | Where the code is assembled to live; `$A000` for a socket ROM |
| `--size` | The size of the image; the rest is filled |
| `--fill` | The fill byte, `$FF` for an erased EPROM by default |
| `--bin` | Write the image |
| `--header` | Write a C++ header of the bytes, which is what the build uses |
| `--symbols` | Write every label's address |
| `--listing` | Write the listing: addresses, bytes and source |

### The dialect

| Directive | Does |
| --- | --- |
| `.org addr` | Where the following code lives |
| `.byte a, b, …` / `.db` | Bytes |
| `.word a, b, …` / `.dw` | Words, low byte first |
| `.fill n, v` | `n` copies of `v` |
| `.res n` | `n` uninitialised bytes |
| `.align n` | Pad to a multiple of `n` |
| `.text "…"` | Bytes of the string, no terminator |
| `.asciiz "…"` | The string and a zero |
| `.include "file"` | Another source file, resolved from the working directory |

Labels are `NAME:` or in column zero; `NAME = expr` is a constant. Identifiers are
uppercased, so `Loop` and `LOOP` are the same label. Expressions may be decimal,
`$hex` or `0x` hex, `%binary`, character literals, `+ - * / % & | ^ << >>`, `<`
(low byte) and `>` (high byte) of a value, and the current address `.`. A number
that runs straight into a letter is an error rather than a zero — `12AB` is
rejected, because it means `$12AB` often enough to be worth saying so.

A 6502 branch that cannot reach its target is assembled as the inverse branch over
`JMP`, so a long branch is written as if it were short and the assembler makes it
work.

---

## `kestrel16_asm` — the 65816 assembler

`kestrel_asm` with the 65816 added: everything above, plus the wider machine.

```bash
cd roms/kestrel16
kestrel16_asm basic.s --name basic --origin 0x10000 --size 65536 \
               --bin ../../build/basic16.bin
```

An origin may be twenty-four bits, so a ROM can be a bank of its own.

### The addressing the 6502 did not have

| Written | Means |
| --- | --- |
| `LDA [$10]` | Direct page indirect long |
| `LDA [$10],Y` | …and indexed |
| `LDA $10,S` | Stack relative |
| `LDA ($10,S),Y` | Stack relative indirect indexed |
| `STA $7E:$2100` | A bank-qualified address; the value becomes 24-bit and picks a long mode |
| `MVN $7E,$7F` | Block move, destination bank first |
| `BRL`, `PER` | Sixteen-bit relative branches |
| `REP #$30`, `SEP #$20` | The implied forms that carry a byte |

An operand that will not fit the short form the mnemonic can encode is widened as
far as the mnemonic allows: direct page, then absolute, then long.

### Immediates

The processor decides how wide an immediate is from the M and X flags at run time,
and the assembler cannot see those flags. So the source says what it means:

| Written | Width |
| --- | --- |
| `LDA #$12` | **One byte** — the 6502's width, and the default |
| `LDA.W #$1234` | Two |
| `LDA.B #$12` | One, said again for a reader |
| `LDA.L $1234` | The twenty-four-bit form of the operand |
| `LDX #$1234` | **Two**, because this machine keeps its index registers sixteen bits wide |
| `LDX.B #$12` | One, where a routine has narrowed the index registers |

An immediate that does not fit the width it was given is an error rather than a
silent truncation: a missing `.W` should not become a mystery on the screen.

---

## `kestrel` — the command line

One executable, two models. It powers a machine on, runs it, types at it and
prints the screen.

```bash
$ ./build/kestrel --model kestrel --type "PRINT 2+2"
*** KESTREL BASIC V2.0 ROM ***
64K RAM SYSTEM
(C) 1982 HONEYBADGER SYSTEMS

READY.
PRINT 2+2
4
READY.
```

| Option | Meaning |
| --- | --- |
| `--model kestrel\|kestrel16` | Which machine to power on; `kestrel` by default |
| `--frames N` | How many frames to run before the screen is printed |
| `--speed 1\|2\|4\|8\|turbo` | How fast the host clocks it; `turbo` by default |
| `--type "LINE"` | Type a line at the prompt and let the machine answer; may be repeated |
| `--rom ID` | Which ROM to put in the 1982 machine's socket: `basic`, `monitor` |
| `--tape FILE` | Insert this cassette image; the machine gets a blank one otherwise |
| `--no-screen` | Print nothing |

The machine types at the speed it could be typed at: a key goes in only when the
keyboard controller has room for it, and each line waits for the buffer to empty
before the next. Pasting a whole line in at once overruns the sixteen-key buffer
and loses characters, Return included — which welds two program lines into one.

```bash
$ ./build/kestrel --rom monitor --type "S 0900 0902" --type "L"
```

`--rom monitor` boots the machine with the monitor in the socket instead of BASIC,
which is how the cassette is used at all: `S` and `L` live there.

---

## `kestrel_computers` — the window

The same machines with a tube you can watch and a keyboard you can use.

```bash
./build/kestrel_computers                    # the Kestrel
./build/kestrel_computers --model kestrel16  # the Kestrel 16
./build/kestrel_computers --shot tube.png    # photograph it and exit, for CI
```

The window offers the machine's speed, its ROM socket and cartridge (on the 1982
machine, which has them), and its disk and cassette. `Ctrl+Q` leaves the machine
on.

---

## The build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

| Target | Builds |
| --- | --- |
| `kestrel_roms` | The Kestrel's ROM images, as headers |
| `kestrel16_roms` | The Kestrel 16's ROM images |
| `kestrel_machine` | The machine, both models |
| `kestrel` | The command line |
| `kestrel_computers` | The window, if Qt 6 Widgets is found |
| `kestrel_*_tests` | The six suites |

Each ROM is assembled by a custom command into `build/generated/`, with a header
of the bytes and a listing beside it. A change to a `.s` file rebuilds the image,
the header and everything that includes it.
