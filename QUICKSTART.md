# Quickstart — HB Computers

Two fictional computers, real emulated CPUs, original machine-code ROMs. Choose the 1982 Kestrel (floating-point BASIC) or the 1992 Kestrel 16 (integer BASIC).

## 1. Get the source and tools

Install Git and Rust using [rustup](https://rustup.rs/). The repository pins Rust 1.95.0. On Linux, install the GUI development packages in [Vagrantfile](Vagrantfile) if building the window.

```sh
git clone https://github.com/James-HoneyBadger/HB_Computers.git
cd HB_Computers
```

## 2. Boot a computer

From the repository root on macOS or Linux:

```sh
./start.sh cli --model kestrel --type "PRINT 2+2"
./start.sh cli --model kestrel16 --type "PRINT 2+2"
```

Both display 4 on the emulated screen. The first build downloads dependencies. For a CLI-only build, without the GUI dependencies:

```sh
cargo build --manifest-path rust/Cargo.toml --locked -p kestrel-cli
rust/target/debug/kestrel --model kestrel --type "PRINT 2+2"
```

## 3. Explore the desktop or terminal

```sh
./start.sh
./start.sh kestrel16
./start.sh emu --model kestrel16
```

In the desktop window, F12 opens settings. In the terminal emulator, /help lists commands and /quit exits.

## 4. Run a program

At either BASIC prompt type:

```basic
10 PRINT "HELLO KESTREL"
20 PRINT 2+2
RUN
```

Or explore the supplied [samples](samples/). Kestrel sample files normally need RUN; Kestrel 16 files may contain immediate commands or their own RUN. Read the source before appending another RUN.

## 5. Learn more

- [Instruction manual](docs/instruction-manual.md): window, keyboard, media and troubleshooting.
- [BASIC comparison](docs/basic-comparison.md): differences between the two languages.
- [Documentation index](docs/README.md): all manuals and technical references.

If a ROM cannot be found, keep the roms/ and cartridges/ directories with the repository or specify the ROM directory through KESTREL_SOURCES. If a disk image matters, keep a backup: mounting a writable disk in the CLI can write it back at exit.
