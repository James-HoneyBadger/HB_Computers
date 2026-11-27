# IMPLEMENTATION STATUS

Accurate status of the HB Computers engineering overhaul. Updated after every
substantive pass. Do not read this file as aspirational.

## Current pass — Stage A: baseline

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
