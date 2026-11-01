# Programmer's Guide

Two jobs, one book: writing code **for** the machines — machine code, ROMs,
routines deposited with the monitor — and changing the **emulator** itself. They
meet at the assemblers, because a change to a ROM is a change to the machine and
the machine's tests are what prove it.

The dialect and the command line are tabulated in [The tools](TOOLS.md); this
book is about using them well.

---

## Contents

- [Writing for the Kestrel](#writing-for-the-kestrel)
- [Writing for the Kestrel 16](#writing-for-the-kestrel-16)
- [Three ways to get an instruction wrong](#three-ways-to-get-an-instruction-wrong)
- [Changing the emulator](#changing-the-emulator)
- [Adding a register](#adding-a-register)
- [The tests](#the-tests)
- [The build](#the-build)
- [House rules](#house-rules)

---

## Writing for the Kestrel

### Where code can live

| Address | What it wants |
| --- | --- |
| `$A000`–`$DFFF` | A 16 KiB socket ROM: Kestrel BASIC, or the monitor |
| `$9000`–`$9FFF` | A 4 KiB cartridge |
| `$0801`–`$7EFF` | Free RAM: where a program is stored, and where the monitor's `:` deposits bytes |

### Trying code without burning a ROM

The monitor ROM is the fast loop: deposit, run, and see what happened.

```
.: 0900 A9 41 20 00 E0 60
.M 0900 6
0900: A9 41 20 00 E0 60
.G 0900
A.
```

`A9 41` is `LDA #'A'`, `20 00 E0` is `JSR $E000` — `CHROUT` — and `60` is `RTS`,
which brings the machine back to the prompt. This is the whole development
cycle a 1982 person had, and it still works here:

```bash
./build/kestrel --rom monitor --type ": 0900 A9 41 20 00 E0 60" --type "G 0900"
```

### Calling the KERNAL

The KERNAL is at `$E000` and knows nothing about any language. Its services are
fixed addresses, so a socket ROM reaches the machine with a `JSR`:

| Address | Service | Arguments |
| --- | --- | --- |
| `$E000` | `CHROUT` | A = character |
| `$E003` | `GETIN` | A = the next key, or `0` |
| `$E006` | `CHRIN` | the next key, waiting |
| `$E009` | `CLS` | |
| `$E00C` | `HOME` | |
| `$E00F` | `PRINT` | the zero-terminated string at `$0A`/`$0B` |
| `$E012` | `SCROLL` | |
| `$E015` | `LOCATE` | X = column, Y = row |
| `$E018` | `GETLINE` | a line into `$0200`, its length in `$15` |
| `$E01B` | `READSECT` | A/X = sector, buffer at `$06`/`$07` |
| `$E01E` | `WRITESECT` | the same three, the other way round |
| `$E021` | `CURSORON` | A = `1` solid, `0` none |

The disk words and the four `DOS*` words beside them are in
[the user guide](users-guide.md#kestrel-dos); their reason comes back in A:
`0` fine, `1` no disk, `2` no such file, `3` no room, `4` the disk failed.

### Writing to the tube without the KERNAL

The screen is memory. A cell is one character at `$8000 + row*40 + column` and
its attribute is the same offset from `$8400`:

```asm
        LDA #'*'
        STA $8000
        LDA #$1E            ; white ink, light blue paper
        STA $8400
```

The cursor registers in the I/O page (`$03`, `$04`) are the KERNAL's idea of
where it is; a program that writes the screen directly should either keep them in
step or not use the KERNAL's printing afterwards.

## Writing for the Kestrel 16

### Banks

| Bank | What |
| --- | --- |
| `$00` | RAM, the screen, the I/O page, the KERNAL at `$E000` |
| `$01` | The language ROM, 64 KiB. This is where BASIC lives |
| `$04`–`$07` | A cartridge: 8 KiB of ROM at `$C000` of each bank and RAM above |

The KERNAL's services are a table of `JSR`/`RTL` entries at `$00:E000`, and the
same list again at `$00:FF00` in four-byte entries — `JSL` and `RTL` — because a
`JSR` from bank one cannot reach bank zero. Code in bank one calls the long
table.

### Two rules the ROMs follow

**The accumulator's width is a convention, not a guess.** `SEP #$20` makes it a
byte, `REP #$20` a word. Every immediate between a `SEP` and its matching `REP`
says `.B` or nothing; every word immediate says `.W`. The assembler cannot see
the flags, so the source says what it means — and an immediate that does not fit
its width is an error rather than a silent truncation.

A width mistake is not a wrong number: the processor fetches the next byte as the
high half of the immediate, and the instruction after it is never executed. A
`AND #1` written inside a `REP #$20` section assembles to two bytes and is read as
four. Say `AND.W #1`.

**The tube's position is the row and column pair.** `ZP_ROW` and `ZP_COL` are the
only copy, and a routine turns them into an address. An offset kept alongside
them drifts, and the drift shows up as text in the wrong place.

### A routine, beginning to end

```asm
; SAY_WHAT: the machine's one complaint, "?WHAT?".
SAY_WHAT:
        LDA #<TXT_WHAT
        STA ZP_PTR
        LDA #>TXT_WHAT
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_WHAT_END - TXT_WHAT
        JSL L_PRINT
        RTS
```

`L_PRINT` is the KERNAL's long entry: a 24-bit pointer and a length, so a string
in any bank can be printed. The zero page registers `ZP_PTR` and its friends are
the machine's own, listed in `roms/kestrel16/kestrel16.inc`.

## Three ways to get an instruction wrong

| Symptom | Cause |
| --- | --- |
| Everything after a certain line misbehaves | A byte immediate inside a sixteen-bit section, as above |
| A jump lands in RAM | `JMP` used where `JML` was meant. `JMP` keeps the program bank; only `JML` changes it |
| A branch a few hundred bytes away does something impossible | A conditional branch that could not reach, and the assembler's inverse-over-`JMP` expansion — read the listing |

The listing is the tool for all three: `--listing` writes addresses, bytes and the
source line side by side, and `--symbols` writes every label's address.

## Changing the emulator

### The shape of it

```
   kestrel (CLI)     kestrel_computers (window)
            \        /
          kestrel_machine          <- include/kestrel/machine.hpp, src/kestrel/machine.cpp
            /            \
     devices (shared)    the two processors
     include/kestrel/devices.hpp       model1982/cpu6502.cpp
                                       model1992/cpu65816.cpp
```

`kestrel::Machine` is built for a `kestrel::Model`, and a profile table says what
that model is. The bus decodes against the profile; the devices are parameterised
by it; the processors are separate types with separate accessors. There is no
common processor interface on purpose: asking a 6502 machine for its 65816 is a
mistake the type system should catch.

### The API a host uses

| Call | Does |
| --- | --- |
| `Machine{Model::Kestrel}` | Power on: RAM cleared, video cleared, shipped ROMs mounted, processor reset |
| `mountKernal(bytes, id)`, `mountBasic(bytes, id)`, `mountSocketRom(rom, id)` | Put a ROM in, refusing a wrong-sized image |
| `setSpeed(Speed)`, `runCycles(n)`, `runFrames(n)` | Pace and run |
| `type(key)`, `typeText(s)`, `typeLine(s)`, `pressBreak()` | The host's hands |
| `screenText()`, `screenLine(r)`, `cursorColumn()`, `frameCounter()` | Reading the machine back |
| `cpu6502()`, `cpu65816()` | The processor, as the model's own type |
| `video()`, `keyboard()`, `disk()`, `tape()` | The devices, for a host that needs them |
| `onSound` | Called when the ROM writes the sound latch, so the host can beep |

Nothing above draws anything for the machine: the host's whole job is to run the
processor, feed the keyboard and read the screen. A host that started painting on
its own would be the thing these machines exist not to be.

### Adding a register

A register is a device address, and it exists in four places. Change them in one
commit:

1. the model's `map.hpp`: the `Register` enum, and the header comment's map;
2. the model's `.inc` for the ROMs;
3. `Machine::layOutRegisters` and the read/write cases in `machine.cpp`;
4. [the technical reference](technical-reference.md#the-io-pages).

A test belongs with it: `kestrel_machine_tests` pokes each register through the
address the ROM uses, which is how the pair of maps is kept honest.

### Adding a device

`src/kestrel/devices.cpp` holds the tube, the keyboard, the disk and the cassette.
A device owns its state and knows nothing about addresses; the machine's decode
decides what a read or a write means, and calls the device. Keeping that line
where it is, is what lets the same cassette serve two machines with different
register layouts.

## The tests

| Suite | Proves |
| --- | --- |
| `kestrel_asm_tests` | the 6502 dialect: every addressing mode, the three ways of writing hex, labels, the long-branch expansion, and the refusals |
| `kestrel_cpu_tests` | the 6502: addressing, arithmetic, decimal mode, flags, cycles, interrupts |
| `kestrel_machine_tests` | the Kestrel: its ROMs boot, typing works, the disk, the tape, the monitor, the language |
| `kestrel16_asm_tests` | every 65816 addressing mode and width rule, byte for byte |
| `kestrel16_cpu_tests` | the 65816: widths, banks, the direct page, jumps, arithmetic, cycles |
| `kestrel16_machine_tests` | the Kestrel 16: its ROMs boot, and typing at it works |
| `kestrel_samples_tests` | every program in `samples/`, against its `.expected` file |

```bash
ctest --test-dir build --output-on-failure      # all of them
./build/kestrel_machine_tests                   # one, with its own output
```

`tests/check.hpp` is the whole framework: `TW_CHECK(condition)` and
`TW_CHECK_EQ(actual, expected)`, a tally, and a non-zero exit code. A failure
prints the file and line and flushes immediately, because a test that crashes
should still have reported the check before it.

A test of a ROM is a keystroke going in and characters coming out of the screen.
That is deliberate: a test that called a ROM routine from C++ would prove the
routine runs, not that the machine works.

## The build

| Target | Builds |
| --- | --- |
| `kestrel_roms`, `kestrel16_roms` | The ROM images, as headers of bytes |
| `kestrel_machine` | The machine, both models, no Qt |
| `kestrel` | The command line |
| `kestrel_computers` | The window, if Qt 6 Widgets is found |
| `kestrel_asm`, `kestrel16_asm` | The assemblers the ROMs are built with |
| `kestrel_*_tests` | The seven suites |

Each ROM is a custom command: `kestrel_asm` or `kestrel16_asm` writes a header
into `build/generated/` and the machine includes it. A change to a `.s` file
rebuilds the image, the header and everything that includes it, so the tests can
never be run against yesterday's ROM.

`./start.sh` wraps the three steps (`build`, `test`, and starting a machine), and
`Vagrantfile` does the same in a clean Ubuntu 22.04 guest if you want a build
machine without touching yours.

## House rules

- **The map is written twice and changed together.** `map.hpp` and the `.inc`
  beside it, in one commit, or the ROMs and the emulator disagree.
- **A ROM change is proved by a machine test.** If typing at the machine cannot
  see the difference, the test is not yet written.
- **The manuals are part of the machine.** If a table and the code disagree, one
  of them is a bug — and the code usually wins.
- **The samples are tests.** A sample with no `.expected` file fails the suite.
- **Nothing on the screen is drawn by the host.** It is the one rule that makes
  these machines machines.
