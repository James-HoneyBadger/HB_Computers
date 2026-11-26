# Rust workspace

The current implementation uses Rust and egui. See [Quickstart](../QUICKSTART.md) for installation and [Architecture](../docs/ARCHITECTURE.md) for the design.

## Build and test

From this directory:

```sh
cargo build --workspace --locked
cargo test --workspace --locked
cargo run --locked --bin kestrel -- --type 'PRINT 2+2*3'
cargo run --locked --bin kestrel -- --model kestrel16 --type 'PRINT 1000*20'
cargo run --locked --bin kestrel_emu
cargo run --locked --bin kestrel_computers
```

The repository pins Rust 1.95.0 using rustup. The GUI requires platform graphics dependencies; consult the root README and Vagrantfile for Linux prerequisites. For headless use without compiling the GUI, build the CLI package only with `cargo build --locked -p kestrel-cli`.

## Workspace layout

| Crate | Responsibility |
| --- | --- |
| kestrel-machine | Profiles, bus, devices, CPU cores and ROM assembly/loading |
| kestrel-asm | 6502 assembler and CLI |
| kestrel16-asm | 65816 assembler and CLI |
| kestrel-cli | Batch CLI and interactive terminal emulator |
| kestrel-app | egui desktop emulator |

ROM sources and cartridges live outside the workspace at the repository root. They are assembled in memory at runtime by default; keep them with a distributed binary or set KESTREL_SOURCES to the ROM directory. Explicit preassembled images use the --roms option.

## Verification

The workspace tests cover both processors, device behavior, assembler encodings, booting the source ROMs, BASIC, cartridges, all 42 sample programs, frontend argument validation and the GUI settings layout. The layout test is not a substitute for interactive desktop testing.

On Linux with a working virtual display and graphics backend, the GUI can capture a screenshot:

```sh
xvfb-run -a cargo run --locked --bin kestrel_computers -- --shot shot.png
```

See [development history](../docs/development-history.md) for the earlier C++ implementation and migration.
