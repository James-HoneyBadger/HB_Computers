# The Rust port

`Rust and egui`, replacing the C++20 and Qt 6 of the tree above — done the only
way a rewrite of this size survives: one crate at a time, each with the tests that
prove it, and the C++ tree kept until the port can do everything it does.

| Crate | Ports | State |
| --- | --- | --- |
| `kestrel-machine` | `include/kestrel/`, `src/kestrel/` | **profile and devices done**, with tests |
| ... | the bus decode and the I/O page | next |
| ... | `cpu6502.cpp` | after that |
| ... | `cpu65816.cpp` | after that |
| `kestrel-roms` | the ROM build (`build.rs` calling the assemblers) | after that |
| `kestrel-cli` | `tools/kestrel.cpp` and `tools/kestrel_emu.cpp` | after that |
| `kestrel-app` | `app/main.cpp` and `window.cpp`, in egui | last |

```bash
cargo test --offline      # from this directory
```

The port follows the original file by file, so the two can be read side by side:
`profile.rs` is `makeProfile`, `devices.rs` is `devices.cpp`. The numbers in the
profile are asserted against the C++ table's values, so a drift in either shows up
as a failing test rather than as a machine that behaves differently.
