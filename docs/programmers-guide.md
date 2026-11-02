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
./start.sh cli --rom monitor --type ": 0900 A9 41 20 00 E0 60" --type "G 0900"
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
| `$E01B` | `READSECT` | A/X = sector low/high, buffer at `$06`/`$07` |
| `$E01E` | `WRITESECT` | the same three, the other way round |
| `$E021` | `CURSORON` | A = `1` solid, `0` none |
| `$E024` | `DOSCATALOG` | |
| `$E027` | `DOSFIND` | the name at `$0710`; A = its entry, or `$FF` |
| `$E02A` | `DOSLOAD` | `$18`/`$19` = where; comes back holding the end |
| `$E02D` | `DOSSAVE` | from `$18`/`$19` to `$1B`/`$1C` |
| `$E030` | `DOSDELETE` | the name at `$0710` |
| `$E033` | `DOSSTATUS` | A = `0`, or an error code |
| `$E036` | `DOSFORMAT` | a blank volume on the disk |

The disk words return their reason in A: `0` fine, `1` no disk, `2` no such
file, `3` no room, `4` the disk itself failed. The Kestrel 16's table is the
same list at `$00:E000` with eleven more entries above `$E036` — ink, paper,
sound, the frame counter, the top of memory, peek, poke, a waiting key, a
newline and the two tape calls; [its owner's manual](kestrel16/README.md)
gives the lot.

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

### A cartridge instead of a ROM

A cartridge is the same idea as a ROM with a shorter life: a program in the
machine from the moment it is switched on, which the reset starts instead of the
language. The KERNAL's `CART_SCAN` looks at the bottom of the slot for the four
letters `KSTL` and jumps to the word after them, so a cartridge is an image with a
six-byte header and no other ceremony. `cartridges/README.md` has the header, the
slot sizes for both machines, and the catalogue of what is already there.

Two things are worth knowing before the first one is written. A cartridge keeps
its state in RAM and not in the zero page, because every call to the KERNAL is
allowed to use every cell of the zero page — `CHROUT` loads the screen pointer
into `ZP_PTR` for each character it prints. And the image has to be exactly the
size of the slot: the machine refuses anything else rather than half-fitting it,
which is why the sources are assembled to the full size with the rest left as
`$FF`.

## Writing for the Kestrel 16

### Banks

| Bank | What |
| --- | --- |
| `$00` | RAM, the screen, the I/O page, the KERNAL at `$E000` |
| `$01` | The language ROM, 64 KiB. This is where BASIC lives |
| `$04`–`$07` | A cartridge, when one is fitted: 64 KiB a bank, read-only. The same image also shows through an 8 KiB window at `$00:C000`–`$DFFF`. Writes still reach the RAM underneath |

The KERNAL's services are a table of `JSR`/`RTL` entries at `$00:E000`, and the
same list again at `$00:FF00` in four-byte entries — `JSL` and `RTL` — because a
`JSR` from bank one cannot reach bank zero. Code in bank one calls the long
table.

### Three rules the ROMs follow

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

**A memory increment is as wide as the accumulator.** `INC addr` moves one byte in
eight-bit mode and two in sixteen, which is why a sixteen-bit pointer is never
stepped by a bare `INC`: the low byte would wrap at the end of the page and the
pointer would start the page again. `SRC_STEP` in the Kestrel 16's language and
`DOS_STEP` in its KERNAL set the width, step the word and put the width back. A
loop that reads text a character at a time is where this goes wrong, and the
symptom is a long program that runs for ever and a short one that never fails.

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
   kestrel (CLI)   kestrel_emu (session)   kestrel_computers (window)
          \               |                      /
           \              |                     /
                   kestrel-machine
                   /            \
        devices (shared)     the two processors
          devices.rs          cpu6502.rs
                              cpu65816.rs
```

One library serves the three programs. The bus decodes against the profile; the
devices are parameterised by it; the processors are separate types behind a
`Cpu` enum, chosen once when the machine is powered on. The processors are
separate on purpose: asking a 6502 machine for its 65816 is a mistake the type
system should catch, and the enum keeps that true without making the machine
generic over its processor.

### The API a host uses

| Call | Does |
| --- | --- |
| `Machine::new(model)` | Power on the bus and the model's processor, with the ROM sockets empty |
| `mount_assembled_roms()`, `mount_shipped_roms(dir)` | Put the ROMs in; a wrongly sized image is refused, never padded |
| `mount_kernal(bytes)`, `mount_basic(bytes)` | Replace one image |
| `reset()` | Take the reset vector, with the ROMs already in place |
| `run_cycles(n)`, `run_frames(n)` | Pace and run |
| `type_key(k)`, `type_text(s)`, `type_line(s)`, `type_line_paced(s)` | The host's hands; the paced form waits for the keyboard buffer |
| `press_break()` | The BREAK line, as a non-maskable interrupt |
| `screen_text()`, `screen_line(row)` | The tube read back as text |
| `instructions()`, `cycles()` | Counters, for a host that shows them |
| `bus.video`, `bus.keyboard`, `bus.disk`, `bus.tape` | The devices themselves |
| `bus.notes_played` | Counts every write to the sound latch, which is what a host watches to make a noise |

Speed is not in the API: the host decides how many cycles to hand the machine,
which is what makes `Machine::run_cycles` the whole of pacing.

Nothing above draws anything for the machine: the host's whole job is to run the
processor, feed the keyboard and read the screen. A host that started painting on
its own would be the thing these machines exist not to be.

### Adding a register

A register is a device address, and it exists in four places. Change them in one
commit:

1. the kind in `rust/crates/kestrel-machine/src/bus.rs`: the `Register` enum and
   the two layout tables beside it;
2. the model's `.inc` for the ROMs;
3. `read_register` and `write_register` in the same file;
4. [the technical reference](technical-reference.md#the-io-pages).

A test belongs with it: the machine's tests poke each register through the address
the ROM uses, which is how the pair of maps is kept honest.

### Adding a device

`rust/crates/kestrel-machine/src/devices.rs` holds the tube, the keyboard, the
disk and the cassette. A device owns its state and knows nothing about addresses;
the bus decides what a read or a write means and calls the device. Keeping that
line where it is, is what lets the same cassette serve two machines with
different register layouts.

## The tests

Every crate carries the tests for what it is:

| Crate | Proves |
| --- | --- |
| `kestrel-asm` | the 6502 dialect: every addressing mode, the three ways of writing hex, labels, the long-branch expansion, and the refusals |
| `kestrel-machine` (`cpu6502`) | the 6502: addressing, arithmetic, decimal mode, flags, cycles, interrupts |
| `kestrel-machine` (`bus`, `devices`) | the decode order of both machines, the I/O page, the disk's two controllers, the tube and the keys |
| `kestrel-machine` (`machine`) | both machines: the ROMs are assembled from `roms/`, boot, and answer at the keyboard |
| `kestrel16-asm` | every 65816 addressing mode and width rule, byte for byte |
| `kestrel-machine` (`cpu65816`) | the 65816: widths, banks, the direct page, jumps, arithmetic, cycles |
| `kestrel-cli` (`samples`) | every program in `samples/`, against its `.expected` file |
| `kestrel-cli` (`cartridges`) | every cartridge's image, and the reset starting the one that is fitted |
| `kestrel-cli` (`emulator`) | the emulator's front end: the model flag chooses the machine, a session switches models, and a command that is not one is refused |

```bash
./start.sh test                     # all of them
cd rust && cargo test -p kestrel-machine   # one crate, with its own output
```

The framework is Rust's: `#[test]`, `assert_eq!` and `cargo test`. What the
assertions say matters more than the framework, so they say what the machine
should have done -- `"sixteen bits, not eight"` beside a number, not a bare
equality.

A test of a ROM is a keystroke going in and characters coming out of the screen.
That is deliberate: a test that called a ROM routine from Rust would prove the
routine runs, not that the machine works.

Two of them are stronger than that. The assembler suites compare assembled bytes
against hand-written expectations, mode by mode and width by width, and the
sample suite types every program in `samples/` at the machine it was written for
and compares the screen with the `.expected` file beside it.

## The build

| Crate | Builds |
| --- | --- |
| `kestrel-machine` | the library: profile, devices, bus, both processors, the machine |
| `kestrel-asm` | `kestrel_asm`, the Kestrel's assembler |
| `kestrel16-asm` | `kestrel16_asm`, the Kestrel 16's assembler |
| `kestrel-cli` | `kestrel`, the command line, and `kestrel_emu`, the interactive one |
| `kestrel-app` | `kestrel_computers`, the window |

Nothing is generated ahead of time. The machine assembles `roms/*.s` with the two
ported assemblers when it starts, once per process, so a change to a `.s` file is
in the next run and the tests can never be run against yesterday's ROM.

`./start.sh` builds what is out of date and starts a machine; `Vagrantfile` does
the same in a clean Ubuntu 22.04 guest if you want a build machine without
touching yours.

## House rules

- **The map is written twice and changed together.** `bus.rs` and the `.inc`
  beside it, in one commit, or the ROMs and the emulator disagree.
- **A ROM change is proved by a machine test.** If typing at the machine cannot
  see the difference, the test is not yet written.
- **The manuals are part of the machine.** If a table and the code disagree, one
  of them is a bug — and the code usually wins.
- **The samples are tests.** A sample with no `.expected` file fails the suite.
- **Nothing on the screen is drawn by the host.** It is the one rule that makes
  these machines machines.
