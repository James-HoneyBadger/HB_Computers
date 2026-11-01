# How the code is put together

One emulator, two models, and two assemblers. Everything else is a ROM image.

```
                kestrel (CLI)      kestrel_computers (window)
                      \                    /
                       \                  /
                        kestrel_machine
                        /             \
              devices (shared)      the two processors
                                     /              \
                            model1982 (6502)   model1992 (65816)
                                     \              /
                                roms/kestrel   roms/kestrel16
                                     ^              ^
                             kestrel_asm     kestrel16_asm
```

## The libraries

| Target | What it is | Needs Qt |
| --- | --- | --- |
| `kestrel_machine` | The machine: both processors, the bus, the tube, the keyboard, the disk, the tape | no |
| `kestrel` | The command line: `--model kestrel\|kestrel16`, type at it, print the screen | no |
| `kestrel_computers` | The window | Qt 6 Widgets |

Apart from Qt for the window, there are no other dependencies: the machine, the
tools and the tests are C++20 and the standard library.

## The machine

`kestrel::Machine` is built for a `kestrel::Model`. The profile table in
`src/kestrel/machine.cpp` says what that model is, and the bus decodes against
it: RAM, the ROM windows, the cartridge, the tube's two arrays and the I/O page.

| File | Part |
| --- | --- |
| `src/kestrel/machine.cpp` | The profiles, the address decode, the I/O page, the frame, the disk commands |
| `src/kestrel/devices.cpp` | The tube, the keyboard, the disk and the tape, parameterised by the profile |
| `src/kestrel/model1982/cpu6502.cpp` | The Kestrel's processor, whole |
| `src/kestrel/model1992/cpu65816.cpp` | The Kestrel 16's processor, whole |
| `src/kestrel/model1982/roms.cpp` | The table of ROMs the 1982 machine can be given |

The two processors are not interchangeable and the machine never substitutes one
for the other: `cpu6502()` and `cpu65816()` are separate accessors over separate
classes, and only the one the model has is stepped, so a fault in one cannot
show up as a fault in the other.

### The two models

| | `model1982` | `model1992` |
| --- | --- | --- |
| Processor | 6502, 64 KiB | 65816, 16 MiB / 24-bit |
| Screen | 40 x 25 at `$8000` | 80 x 50 at `$008000` |
| I/O | `$8800` | `$A000` |
| Language | socket ROM at `$A000` | bank `$01` |
| Disk | 512 x 256 | 4096 x 512 |

Each model's map has exactly one home — `include/kestrel/model1982/map.hpp` and
`include/kestrel/model1992/map.hpp` — and `roms/kestrel/kestrel.inc` and
`roms/kestrel16/kestrel16.inc` repeat them for the ROMs. The pairs are changed
together, in the same commit.

## The ROMs

The languages are not C++: they are `roms/kestrel/*.s` and `roms/kestrel16/*.s`,
assembled at build time by `kestrel_asm` and `kestrel16_asm` into headers of
bytes that the machine is given. A change to a ROM is a change to the machine,
and the machine tests prove it by typing at the machine and reading the screen
back.

## The tests

| Suite | What it proves |
| --- | --- |
| `kestrel_asm_tests` | The dialect the 6502 ROMs are written in |
| `kestrel_cpu_tests` | The 6502: addressing, arithmetic, flags, cycles, interrupts |
| `kestrel_machine_tests` | The Kestrel: its ROMs boot, and typing at it works |
| `kestrel16_asm_tests` | Every 65816 addressing mode and width rule, byte for byte |
| `kestrel16_cpu_tests` | The 65816: widths, banks, the direct page, arithmetic, cycles |
| `kestrel16_machine_tests` | The Kestrel 16: its ROMs boot, and typing at it works |

`ctest --test-dir build` runs all of them.
