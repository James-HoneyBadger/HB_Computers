# NEXT ACTIONS

Concrete next implementation tasks, in dependency order. Marked off by editing
this file and `IMPLEMENTATION_STATUS.md` as work lands. Each item names where to
look and what evidence completes it.

## Immediate (Stage B — correctness and data integrity)

1. ~~**CPU conformance matrix.**~~ Done — see `docs/cpu-conformance.md`.
2. **CPU defect hunt (ongoing).** Both cores read in full; one 6502 defect fixed
   (RMW page-cross cycle). Remaining: wire in a third-party conformance vector
   suite (e.g. Klaus Dormann 6502 functional tests) and record provenance;
   re-verify the 6502 decimal-mode flag edge cases with vector evidence.
   Evidence: vector suite runs green; any new defect gets a regression test.
3. **Memory-map and bus decode tests.** `bus.rs` has inline tests in `lib.rs`;
   extend them for boundary addresses, ROM overlays, the Kestrel 16 `$A100–$BFFF`
   region, cartridge windows, bank transitions, read-only regions and writes
   beneath overlays. First resolve whether the `$A100–$BFFF` zero read is intended
   (check `roms/kestrel16/*.inc` and the C++ history). Evidence: tests in
   `kestrel-machine`.
4. **BASIC feature inventory.** Extract a feature inventory from the ROM assembly
   (`roms/kestrel/*.s`, `roms/kestrel16/*.s`) and from executable tests, not from
   docs alone. Compare with `docs/users-guide.md` and the two owner's manuals.
   Fix correctness defects before adding features. Evidence: inventory in the
   language docs; defects fixed with tests.
5. **Malformed-input and resource-exhaustion tests.** For the assemblers and the
   machine (program storage, variable tables, string heap, tape/disk images):
   malformed and incomplete statements, boundary values, exhaustion. Evidence:
   controlled errors, no panics, in `kestrel-asm`, `kestrel16-asm`, and
   `kestrel-machine`.
6. **Safe file operations audit.** Walk every filesystem write in the CLI and
   app (disk save, tape save, screenshot, settings). Ensure atomic replacement
   and that a failed save preserves the original. Evidence: tests plus code
   inspection notes.

## Next (Stage C — architecture and usability)

7. **Decompose `kestrel-app/src/main.rs`** (2,384 lines) into cohesive modules
   (lifecycle, machine control, display rendering, input, audio, menus, settings,
   media, prompts, helpers). Keep behavior identical; verify with the existing
   `arguments.rs` test and a manual `--shot` run.
8. **Split `kestrel-machine/src/machine.rs`** (1,928 lines) so scheduling and
   execution are separated from the frontend-facing API and the integration test
   bulk.
9. **Execution-state model.** Formalize powered-off/running/paused/halted/faulted
   states; ensure pause/resume/single-step behave; verify deterministic headless
   runs (same inputs → same frames).

## Later (Stages D–G)

10. Integrated debugger and disassembler (side-effect-free inspection).
11. Versioned save states with atomic save.
12. Disk/tape image explorers (read-only by default).
13. Paced clipboard paste and documented key mapping.
14. Optional CRT/monochrome display modes.
15. BASIC enhancements (INPUT/GET for Kestrel 16, DATA/READ/RESTORE, etc.) in
    assembly only.
16. Original software catalogue and educational curriculum.
17. Documentation overhaul with real screenshots.
18. Distribution packaging and cross-platform CI.
19. Final acceptance pass (Stage G).

## Housekeeping

- Install `cargo-clippy` for the pinned toolchain and run it; fix or document
  warnings.
- Keep `IMPLEMENTATION_STATUS.md` updated after every pass.
