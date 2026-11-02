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
| `--header` | Write a C header of the bytes, for a host that wants them as C |
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
$ ./start.sh cli --model kestrel --type "PRINT 2+2"
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
| `--file FILE` | Type every line of a file, in order, before any `--type` lines |
| `--rom ID` | Which ROM to put in the 1982 machine's socket: `basic`, `monitor` |
| `--tape FILE` | Insert this cassette image; the machine gets a blank one otherwise |
| `--no-screen` | Print nothing |

The machine types at the speed it could be typed at: a key goes in only when the
keyboard controller has room for it, and each line waits for the buffer to empty
before the next. Pasting a whole line in at once overruns the sixteen-key buffer
and loses characters, Return included — which welds two program lines into one.

```bash
$ ./start.sh cli --rom monitor --type "S 0900 0902" --type "L"
```

`--rom monitor` boots the machine with the monitor in the socket instead of BASIC,
which is how the cassette is used at all: `S` and `L` live there.

`--file` is how a program in `samples/` is run: the lines are typed in order, at
the speed a person could type them, so a numbered line is stored and a `RUN`
afterwards runs it.

```bash
$ ./start.sh cli --model kestrel --file samples/kestrel/02-sieve.bas --type RUN
$ ./start.sh cli --model kestrel16 --file samples/kestrel16/03-division.bas
```

---

## `kestrel_emu` — the interactive emulator

The same two machines as the command line, with a session around them: pick a
model, type at it, watch the tube, and change your mind about which machine you
are sitting at.

```bash
./start.sh emu                    # asks which machine when there is a keyboard
./start.sh emu --model kestrel16
```

```text
Which machine?
  1  Kestrel      (6502, 1982)
  2  Kestrel 16   (65816, 1992)
> 2
Kestrel 16 powered on. /help lists the commands; a line without a
leading / is typed at the machine.
+--------------------------------------------------------------------------------+
|KESTREL 16 BASIC 2.0|
|READY>|
+--------------------------------------------------------------------------------+
/> PRINT -7/2
+--------------------------------------------------------------------------------+
|KESTREL 16 BASIC 2.0|
|READY> PRINT -7/2|
|-3|
||
|READY>|
+--------------------------------------------------------------------------------+
/> /quit
kestrel_emu: Kestrel 16 off.
```

A line beginning with `/` is a command for the program; anything else is typed at
the machine, a key at a time and at the speed the machine could be typed at.

| Command | Does |
| --- | --- |
| `/model kestrel\|kestrel16` | power that machine on. A different model is a different machine, so it is a cold boot |
| `/speed 1\|2\|4\|8\|turbo` | how fast the host clocks it |
| `/frames N` | frames to run after each line typed (default 20) |
| `/run [N]` | run N frames (default 50) |
| `/break` | the BREAK key |
| `/rom basic\|monitor` | which ROM is in the Kestrel's socket |
| `/screen` | draw the tube |
| `/status` | clock, frame, cycles |
| `/quit` | leave |

The tube is drawn without its blank tail, since a terminal is not a 40 × 25
monitor. When the input is a pipe — a script — screens are drawn only where
`/screen` asks for one and once at the end, because a screen after every line is
noise when nobody is watching.

| Option | Meaning |
| --- | --- |
| `--model` | which machine; asked for when there is a keyboard and no flag |
| `--speed` | as above |
| `--frames` | as above |
| `--rom` | as above |
| `--type "LINE"` | type a line before the prompt appears; may be repeated |
| `--file FILE` | type every line of a file, in order |

---

## `kestrel_computers` — the window

The same machines with a tube you can watch and a keyboard you can use.

```bash
./start.sh                    # the Kestrel
./start.sh --model kestrel16  # the Kestrel 16
./start.sh --shot tube.png    # photograph it and exit, for CI
```

The window offers the machine's speed, its ROM socket and cartridge (on the 1982
machine, which has them), and its disk and cassette. `Ctrl+Q` leaves the machine
on.

---

## The build

```bash
cd rust
cargo build --release    # the three programs, in rust/target/release
cargo test               # every suite
```

| Crate | Builds |
| --- | --- |
| `kestrel-machine` | the library: profile, devices, bus, both processors, the machine |
| `kestrel-asm` | `kestrel_asm`, the Kestrel's assembler |
| `kestrel16-asm` | `kestrel16_asm`, the Kestrel 16's assembler |
| `kestrel-cli` | `kestrel` and `kestrel_emu` |
| `kestrel-app` | `kestrel_computers`, the window |

There is no ROM build step: the programs assemble `roms/*.s` when they start, with
the same two assemblers, once per process. A change to a `.s` file is therefore in
the next run, and a test can never be run against yesterday's ROM.
