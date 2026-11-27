# IMPLEMENTATION STATUS

Accurate status of the HB Computers engineering overhaul. Updated after every
substantive pass. Do not read this file as aspirational.

## Current pass — Stage B: correctness (CPU, bus, CLI)

### Work completed

1. **Read both processor cores in full** (`cpu6502.rs`, `cpu65816.rs`, and the
   65816 tests) and audited them against the directive's CPU checklist.
2. **Fixed a 6502 cycle-accounting defect.** RMW instructions with an
   `absolute,X`/`absolute,Y` operand were charged an extra cycle on a page cross;
   the NMOS 6502 fixes them at seven cycles. Commit `e8849a1`.
3. **Wrote `docs/cpu-conformance.md`** — an explicit conformance matrix for both
   cores, linked from `docs/README.md`.
4. **Fixed a Kestrel 16 bus write-decode defect.** Addresses `$9F40–$9FFF`
   (between attribute memory and the I/O page) read as RAM but silently dropped
   writes. The write decode now lands them in backing RAM, matching the read
   decode and the documented map. Commit `3f0286e`; regression test added.
5. **Added `--version`/`-V` to all four binaries** (`kestrel`, `kestrel_emu`,
   `kestrel_asm`, `kestrel16_asm`), each exiting zero and reporting the workspace
   version. Also fixed `kestrel16_asm`'s usage text, which named the wrong tool.
   Commit `7e8cc48`; regression tests added.
6. **Verified assembler and CLI exit codes** (malformed source → 1, no source →
   2, unknown option → 2, bad model/frames → 2, runtime error → 1).

### Files changed (this pass)

- `rust/crates/kestrel-machine/src/cpu6502.rs` — RMW cycle fix + test.
- `rust/crates/kestrel-machine/src/bus.rs`, `src/lib.rs` — Kestrel 16 write-gap
  fix + test.
- `rust/crates/kestrel-cli/src/main.rs`, `src/bin/kestrel-emu.rs`,
  `rust/crates/kestrel-asm/src/main.rs`, `rust/crates/kestrel16-asm/src/main.rs`
  and their test files — `--version`.
- `docs/cpu-conformance.md` (new), `docs/README.md`.

### Actual test commands and results

```
cd rust && cargo test --workspace --locked
```

Result: **all green — 146 tests passed, 0 failed** (85 machine, 44 CLI,
7 + 7 assembler, 3 app) after all changes.

```
python3 scripts/check_docs.py rust/target/debug/kestrel
```

Result: local Markdown links OK; both machines' documented BASIC examples OK.

### Defects corrected

- 6502 RMW `abs,X`/`abs,Y` page-crossing penalty (8 cycles → 7).
- Kestrel 16 bank-zero write decode for `$9F40–$9FFF` (writes dropped → RAM).
- `kestrel16_asm` usage text naming the wrong tool.

### Known limitations

- No third-party CPU conformance vectors wired in yet.
- Neither core claims bus-level cycle accuracy.

## Earlier pass — Stage A: baseline

### Work completed

1. **Repository reconnaissance.** Inspected all local and remote branches, PRs,
   commit history and Actions results.
   - Local `main` was clean at `6a8621c` ("Merge pull request #1").
   - Remote branches: `origin/main`, `origin/docs/manual-refresh` (1 unmerged
     commit, open PR #2), `origin/fix/repository-readiness` (fully merged via
     PR #1).
   - PR #1 (`fix/repository-readiness`) is merged; PR #2 (`docs/manual-refresh`)
     is open.
   - Recent CI runs on all branches were green (Build and test; Copilot Code
     Review).
2. **Integrated the validated documentation branch.** Fast-forwarded
   `origin/docs/manual-refresh` (`2212e7b`) into `main`. Its claims were verified
   against the implementation before merging (see `ARCHITECTURE_DECISIONS.md`).
3. **Ran the complete regression suite** and recorded the result below.
4. **Created the persistent engineering records** (`MASTER_ENGINEERING_DIRECTIVE.md`,
   `ARCHITECTURE_DECISIONS.md`, `IMPLEMENTATION_STATUS.md`, `NEXT_ACTIONS.md`).

### Files changed (this pass)

- Merged from the documentation branch: `.github/workflows/ci.yml`, `QUICKSTART.md`
  (new), `README.md`, `docs/*`, `rust/README.md`, `scripts/check_docs.py` (new).
- New records: `MASTER_ENGINEERING_DIRECTIVE.md`, `ARCHITECTURE_DECISIONS.md`,
  `IMPLEMENTATION_STATUS.md`, `NEXT_ACTIONS.md`.

### Actual test commands and results

```
cd rust && cargo test --workspace --locked
```

Result: **all green — 142 tests passed, 0 failed.**

Breakdown by crate:

| Crate | Tests passed |
| --- | --- |
| `kestrel-machine` (unit) | 84 |
| `kestrel-cli` (arguments, cartridges, emulator, pilot, roms, samples, socket_rom) | 43 |
| `kestrel-asm` (assembler) | 6 |
| `kestrel16-asm` (assembler) | 6 |
| `kestrel-app` (unit + arguments) | 3 |

```
cd rust && cargo build --workspace --locked   # succeeded
python3 scripts/check_docs.py rust/target/debug/kestrel
```

Result: `Local Markdown file links: OK`; both machines' documented BASIC examples
OK.

### Architectural decisions

See `ARCHITECTURE_DECISIONS.md`.

### Defects corrected

None in the machine core yet. The documentation branch corrected stale manual
claims (string `PRINT A$` behavior, Kestrel 16 cartridge bank behavior, memory
map wording).

### Features implemented

None beyond the documentation refresh. This is the baseline.

### Known limitations

- `cargo-clippy` is not installed for the pinned toolchain in this environment;
  clippy has not yet been run locally (it runs in CI if configured).
- The GUI window has not yet been exercised interactively in this pass.
- The platform matrix (Windows) is not yet validated; CI currently covers
  Ubuntu and macOS.

### Remaining work

See `NEXT_ACTIONS.md` for the concrete queue. In summary: Stage B (CPU, memory,
BASIC correctness) is next.

### Relevant commits

- `2212e7b` — Refresh manuals, add quickstart and validate documentation examples
  (fast-forwarded into `main`).

### Current CI status

Green at baseline on `ubuntu-latest` and `macos-latest` for the prior commits;
local full suite green on Linux.
