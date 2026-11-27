# ARCHITECTURE DECISIONS

This file records architectural decisions and findings for HB Computers. It
describes what the architecture is and why, based on inspection of the source,
not aspiration. It is updated as decisions change.

Status as of the initial baseline (main @ `2212e7b` after integrating the
validated documentation branch).

## Workspace shape

The Rust tree is one Cargo workspace (`rust/Cargo.toml`) with five crates and a
strict, acyclic dependency direction:

```
kestrel-asm ─────┐
                 ├──> kestrel-machine ──> kestrel-cli
kestrel16-asm ───┘                  └──> kestrel-app
```

- `kestrel-machine` — profiles, devices, bus decode, both processors, ROM
  assembly at startup. Depends only on the two assemblers.
- `kestrel-asm` / `kestrel16-asm` — the two assemblers, each both a library and a
  binary. No external dependencies.
- `kestrel-cli` — the `kestrel` command line and the `kestrel_emu` interactive
  session. Depends on `kestrel-machine`.
- `kestrel-app` — the `kestrel_computers` window (egui). Depends on
  `kestrel-machine`, plus `eframe` 0.37 and `image` 0.25 (PNG only, for
  screenshots).

External dependencies are deliberately minimal: only `eframe` (and its transitive
egui/winit/graphics stack) and `image`. The two assemblers and the machine core
have no external dependencies, which keeps the headless path dependency-light.

## Decisions confirmed as sound (preserve)

1. **Rust and egui retained.** The directive requires retaining them unless a
   demonstrably superior alternative is justified by measurable benefit. No such
   case exists; the current stack is small, testable and dependency-light.
2. **ROMs are assembled from source at startup.** `kestrel-machine/src/roms.rs`
   assembles `roms/**/*.s` with the in-tree assemblers once per process. This
   preserves source-assembled ROM functionality and avoids generated binaries in
   the tree. `--roms DIR` loads preassembled images as the explicit alternative.
3. **A profile table is the machine-specific source of truth for sizes and
   addresses** (`kestrel-machine/src/profile.rs`), with model-specific decode in
   `bus.rs`. The doc/manual/map triple is noted in the manuals as "changed
   together in the same commit".
4. **Guest BASIC is genuine guest machine code.** There is no BASIC interpreter in
   Rust; the language ROMs execute on the emulated CPUs.
5. **The emulator state and the frontend state are separated:** the machine lives
   in `kestrel-machine` (headless) and the window (`kestrel-app`) only renders it
   and sends keys/media.

## Decisions made during this pass

1. **Integrate the validated documentation branch** (`origin/docs/manual-refresh`,
   commit `2212e7b`) by fast-forward. Its technical claims were verified against
   the implementation before merging (see below). This is documented here rather
   than silently.

## Findings that remain open (not yet acted on)

1. **`kestrel-app/src/main.rs` is 2,384 lines** in a single file: the whole
   application (settings pages, menus, media dialogs, display rendering, prompts)
   plus small helpers. This is the "large main module" the directive flags and is
   the primary Stage C decomposition target.
2. **`kestrel-machine/src/machine.rs` is 1,928 lines**: the execution loop, the
   frontend-facing API, and the bulk of the machine integration tests. Candidate
   for splitting the execution/scheduling portion from the test-heavy API portion.
3. **The two assemblers duplicate expression parsing and source handling**
   (`expr.rs`, `source.rs`, `token.rs` in both `kestrel-asm` and `kestrel16-asm`).
   Some duplication is justified by distinct processor semantics (opcode tables,
   addressing modes); the shared structural parts (source include handling,
   expression evaluation shape) are candidates for consolidation only where it
   does not obscure the distinct semantics.
4. **Kestrel 16 bank-zero read decode:** addresses `$A100–$BFFF` (above the I/O
   page and below the cartridge window) read zero, which is now documented in the
   technical reference. Whether this is intended versus an oversight requires
   checking the ROM include files and the C++ history before changing.

## Verification performed before integrating the documentation branch

The branch's new `scripts/check_docs.py` claims were reproduced against the
built `kestrel` binary:

- `PRINT 2+2` → `4` on both machines.
- `A$="HELLO"` then `PRINT A$` → `HELLO` on both machines (the prior
  `?WHAT?` claim in the old users-guide was stale).
- `PRINT 30000+1` → `30001` and `PRINT 1000*20` → `20000` on both machines.

The cartridge window claims were checked against `bus.rs`:

- Kestrel 16 bank zero `$C000–$DFFF` reads the cartridge's first 8 KiB; writes
  there are ignored. Confirmed.
- Banks `$04–$05` read the cartridge image, `$06–$07` read `$FF` beyond the
  128 KiB image (and zero when no cartridge is fitted); writes in banks
  `$04–$07` reach backing RAM. Confirmed.
