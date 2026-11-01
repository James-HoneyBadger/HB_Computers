# The documentation

Four books, two references, and the machines' own manuals. The books are written
to be read; the references are for looking things up.

| Book | Read it when | What is in it |
| --- | --- | --- |
| [Instruction Manual](instruction-manual.md) | you have just unboxed the machines | what they are, how to build and start them, the keyboard, the window, the media, your first session, what to do when it goes wrong |
| [User Guide](users-guide.md) | you want to write a program | Kestrel BASIC from `PRINT` to `DEF FN`, the monitor ROM, Kestrel DOS, Kestrel 16 BASIC, errors, worked examples, the samples |
| [Technical Reference](technical-reference.md) | you need an address, a register or a format | both memory maps, both I/O pages, the colours, the bus decode order, interrupts and the frame, the disk layouts, the cassette, the ROM images, the processors |
| [Programmer's Guide](programmers-guide.md) | you are writing for the machines, or changing the emulator | the assemblers' pitfalls, the KERNAL's two tables, the C++ API, adding a register, the eight test suites, the build, the house rules |
| [The tools](TOOLS.md) | you need an option rather than an explanation | both assemblers' full dialect and options, the command line, the window, the build targets |
| [Architecture](ARCHITECTURE.md) | you are finding your way around the source | the libraries, the machine's parts, which file holds what |

And the machines' own books, which are the long form of the language and hardware
chapters:

| Machine | Book |
| --- | --- |
| Kestrel (1982) | [Owner's Manual](kestrel/README.md) — the 6502, the map, the I/O page, the KERNAL, Kestrel DOS, the language, the errors, the limits |
| Kestrel 16 (1992) | [Owner's Manual](kestrel16/README.md) — the 65816, the banks, the KERNAL's two tables, the language, the limits |

## Where the facts come from

Documentation is part of this machine, not a description of it. Every table in
these books is written against a source in the tree:

- **maps and registers** — `include/kestrel/model1982/map.hpp` and
  `include/kestrel/model1992/map.hpp`, and the `.inc` files beside the ROMs;
- **behaviour** — the ROMs themselves, and the suites that type at them:
  `tests/kestrel_machine_tests.cpp`, `tests/kestrel16_machine_tests.cpp`;
- **the processors** — `tests/kestrel_cpu_tests.cpp`,
  `tests/kestrel16_cpu_tests.cpp`;
- **the samples** — `samples/`, with a `.expected` file beside each program.

If a book and a machine disagree, one of them is a bug. The rule in this tree is
that a change to the machine, its maps, or its dialect carries its documentation
with it, in the same commit.

## Reading order

1. **Just want to see it run:** [Instruction Manual](instruction-manual.md), then
   `./start.sh`.
2. **Just want to program it:** the [User Guide](users-guide.md), and
   [the samples](../samples).
3. **Want to know how it works:** the
   [Technical Reference](technical-reference.md), then
   [Architecture](ARCHITECTURE.md).
4. **Want to write machine code:** the
   [Programmer's Guide](programmers-guide.md), then [The tools](TOOLS.md).
