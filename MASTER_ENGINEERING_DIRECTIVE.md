# HB COMPUTERS — MASTER ENGINEERING DIRECTIVE

**Project:** HB Computers
**Organization:** Honey Badger Universe
**Repository:** <https://github.com/James-HoneyBadger/HB_Computers>
**License:** MIT
**Development model:** Independent, community-friendly, local-first FOSS

This file is the canonical record of the engineering directive under which HB
Computers is overhauled. The other persistent records — `IMPLEMENTATION_STATUS.md`,
`ARCHITECTURE_DECISIONS.md` and `NEXT_ACTIONS.md` — describe the state of the
work, not aspirations.

## 1. Primary mission

Conduct a comprehensive, implementation-oriented engineering overhaul of HB
Computers. Transform the existing Kestrel and Kestrel 16 emulators into a
mature, cohesive, polished, technically credible retrocomputing ecosystem.

This is not a code review, a documentation exercise, a superficial GUI facelift
or a bug-fixing assignment. Substantially improve every appropriate aspect:
architecture, correctness, reliability, performance, usability, capabilities,
documentation, testing, tooling, maintainability, portability and distribution.

Investigate first. Implement second. Validate continuously. Carry out the
improvements; do not merely recommend them. Preserve sound implementations.

## 2. Foundational project philosophy

HB Computers is a hobbyist, open-source project within the Honey Badger
Universe. It is not a commercial product. Non-negotiable principles:

- No advertising, telemetry, analytics, mandatory accounts, cloud dependencies,
  subscriptions, proprietary runtime service dependencies, or monetization.
- No unnecessary internet access; no artificial feature restrictions; no
  mandatory AI services.
- Preserve the canonical MIT license. Respect user ownership of files, programs,
  media and configurations. Make the source accessible to independent developers.
- The application operates fully offline after its dependencies are installed.
  Build-time dependency retrieval is acceptable; runtime network activity is not.
- Design for longevity rather than commercial growth.

## 3. Preserve the identity of the machines

Two fictional home computers.

### Kestrel — 1982

MOS 6502 at a nominal 1 MHz, 32 KiB physical RAM, 64 KiB address space,
40 × 25 character display, five-byte floating-point BASIC, KERNAL and language
ROM architecture, disk/tape/cartridge/keyboard/sound devices, alternate language
ROMs (monitor, PILOT, diagnostics).

### Kestrel 16 — 1992

WDC 65C816 at a nominal 8 MHz, 1 MiB backing RAM, 24-bit address space,
80 × 50 character display, banked memory, signed sixteen-bit integer BASIC,
disk/tape/cartridge, KERNAL and language ROMs.

Verify all technical parameters against the implementation before treating
documentation as authoritative. Preserve the fictional history, personality and
relationship between the two machines. Do not turn either into a disguised
Commodore, Apple II, Atari, IBM PC or generic modern virtual machine.

## 4. Critical emulation boundary

The guest computers remain actual emulated computers. The host supplies CPU
execution, memory and address decoding, hardware devices, display rendering,
keyboard handling, media management, debugging, editing tools, configuration,
state serialization and host-side conveniences. The host must not silently
replace guest functionality with native implementations: BASIC executes as guest
machine code, guest file operations use emulated hardware and guest software, and
guest operating-system routines execute through the emulated CPU. A BASIC
interpreter written in Rust disguised as machine-code emulation is unacceptable.
Preserve source-assembled ROM functionality. Optional host-assisted functionality
must be explicitly identified and must not undermine ordinary emulation.

## 5. Repository reconnaissance and baseline

Before modifying: inspect all branches, determine merged state, review open PRs,
inspect commit history and Actions results, preserve uncommitted maintainer work,
identify the authoritative implementation, inspect all Rust crates, ROM and
cartridge assembly sources, BASIC samples and expected outputs, manuals and
technical references, the build system and distribution strategy, and dependency
licensing. Run the complete regression suite and record actual results.

Recent maintenance (canonical MIT licensing, pinned Rust toolchain, frontend
argument validation, macOS/Linux CI, documentation refactoring) must not be
reverted. The documentation branch may contain unmerged work: examine it and
integrate validated changes without losing concurrent work.

Persistent engineering records: `MASTER_ENGINEERING_DIRECTIVE.md`,
`IMPLEMENTATION_STATUS.md`, `ARCHITECTURE_DECISIONS.md`, `NEXT_ACTIONS.md`.

## 6. Architecture assessment

Five principal crates: `kestrel-machine`, `kestrel-asm`, `kestrel16-asm`,
`kestrel-cli`, `kestrel-app`. Evaluate responsibilities, boundaries, dependency
direction and cohesion. Retain Rust and egui unless a demonstrably superior
alternative is justified by measurable benefits. Refactor large modules into
cohesive components — the GUI's large main module is an obvious candidate.
Investigate large modules, duplication, coupling, inconsistent abstractions,
circular dependencies, fragile state management, error handling, testability,
public API, and separation of emulator state from frontend state. Avoid excessive
abstraction, needless traits, premature generic frameworks and micro-crates.

## 7. CPU correctness audit

For the 6502: legal opcode coverage, undocumented-opcode policy, addressing
modes, page-boundary behavior, branch timing, stack behavior, decimal arithmetic,
flags, reset, IRQ/NMI, interrupt return, indirect addressing quirks,
read-modify-write semantics, cycle accounting, illegal-instruction handling.

For the 65C816, additionally: emulation/native modes, accumulator and index
widths, direct page, data bank and program bank registers, stack modes, long
addressing, bank boundaries, block moves, status transitions, interrupt vector
selection, width restoration, decimal arithmetic in both widths.

Create an explicit processor conformance matrix. Use independently maintained,
legally redistributable test vectors where appropriate; record provenance and
licensing. Distinguish functional instruction correctness, instruction-level
cycle accounting, and bus-level cycle accuracy. Never claim cycle-perfect
emulation without evidence. Add regression tests for every discovered defect.

## 8. Machine execution and timing

Audit the execution loop and scheduler: nominal clock pacing, frame generation,
interrupt scheduling, timing drift, turbo operation, pause/resume, single
stepping, execution budgets, host CPU consumption, GUI responsiveness, behavior
during expensive guest operations. The GUI must not become unresponsive. Avoid
uncontrolled busy loops. Create a clear execution-state model (powered off,
running, paused, halted, faulted). A dedicated emulation worker is permissible
with defined ownership and synchronization. Preserve deterministic headless
execution.

## 9. Memory, bus and hardware correctness

Audit address decoding against machine profiles and ROM include files. Test
boundary addresses, RAM limits, ROM overlays, unmapped memory, screen and
attribute memory, I/O registers, cartridge mapping, bank transitions, read-only
regions, writes beneath overlays, out-of-range device accesses, and device reset
behavior. Resolve discrepancies between implementation and manuals. Do not
arbitrarily change established memory maps; document compatibility-sensitive
behavior and migration options. A declarative source of truth for hardware
definitions is acceptable only if it genuinely reduces drift.

## 10. ROM system and assembler development

Audit both assemblers and the ROM assembly system: expression parsing, overflow,
symbol resolution, forward references, duplicate labels, branch range validation,
include processing (recursion, depth limits), addressing mode selection, operand
width, diagnostic accuracy, source location reporting, binary and listing output,
symbol tables, reproducible assembly. Reduce duplication between assemblers
without destroying their distinct processor semantics. Improve compiler-style
diagnostics (filename, line, column, description, context). Malformed assembly
must produce controlled errors, not panics.

Review ROM discovery and packaging. Distinguish bundled ROM sources,
maintainer-provided overrides, explicit preassembled images, user-created
language ROMs and user-created cartridges. A distributed application must not
accidentally load ROM sources from the developer's build machine.

## 11. KERNAL engineering

Review both KERNALs: boot sequences, reset, warm starts, service tables, screen
output, character and keyboard handling, interrupt handlers, disk/tape services,
cartridge startup, register preservation, calling conventions, error handling.
Document stable guest-facing interfaces. Add assembly-level integration tests
that call guest KERNAL routines. Preserve existing entry points unless a
compelling defect requires a compatibility-aware change.

## 12. BASIC language correctness

Build a feature inventory from actual assembly code and executable tests.
Investigate lexical analysis, parsing, precedence, unary operators, numeric
conversion, floating-point accuracy, integer overflow, division, comparisons,
booleans, strings, arrays, user functions, program storage, line editing,
variable initialization, execution, loops, subroutines, conditionals, error
recovery, memory exhaustion, disk and tape operations. Test malformed and
incomplete statements, boundary values, resource exhaustion and nested control
structures. Verify errors leave the machine coherent. Correct defects before
adding features.

## 13. BASIC language expansion

After correctness, evaluate meaningful improvements: INPUT/GET for Kestrel 16,
DATA/READ/RESTORE, enhanced string and numeric functions, more flexible variable
naming, improved editing, additional control flow, better error descriptions,
additional disk commands, random numbers, program interruption, listing controls.
Respect ROM and RAM capacity, historical character, instruction costs and dialect
compatibility. Implement guest language functionality in assembly, not Rust.
Every new feature needs syntax documentation, correctness tests, error-case
tests, a working example, compatibility assessment and resource impact
assessment.

## 14. Alternate language ROMs

Review monitor, PILOT and diagnostics ROMs. Consider additional original language
ROMs (advanced monitor, stack-oriented language, Forth-style environment,
educational assembly environment) as optional expansion after core reliability.
Do not ship third-party copyrighted ROM images without redistribution rights.

## 15. Desktop application redesign

Make the GUI feel like a mature desktop application without sacrificing
retrocomputing charm. Improve visual hierarchy, alignment, spacing, typography,
menus, discoverability, consistency, error messages, keyboard shortcuts, focus,
resizing, HiDPI, accessibility, status indicators, dialogs, confirmations and
configuration persistence. Organize primary menus by actual functionality
(File/Machine/Media/View/Development/Debug/Tools/Settings/Help). Provide sensible
defaults and a clear split between everyday and advanced controls.

## 16. Authentic visual experience

Improve guest display rendering while preserving character and attribute data:
pixel-perfect integer scaling, correct aspect ratio, full-screen, authentic
glyphs, configurable palettes, crisp rendering, optional scanlines/phosphor/
curvature/persistence/monochrome modes. All visual effects optional; the default
remains highly readable. Do not alter guest memory for aesthetic effect. Keep
rendering overhead reasonable. Verify at 40 × 25 and 80 × 50.

## 17. Keyboard and input

Review character mapping, modifiers, function keys, cursor keys, Backspace/Delete,
clipboard paste, host layouts, international keyboards, repeat behavior, focus
transitions, queue capacity, key loss, paste pacing, BREAK/reset shortcuts.
Introduce reliable paced paste. Document host-to-guest conversion. Avoid
silently discarding unsupported characters. Configurable shortcut mappings if
justified.

## 18. Audio and sound

Investigate the sound latch and host audio. Ensure behavior matches documented
hardware. Improve volume, muting, timing, host compatibility, latency and device
failure handling. Do not silently add sophisticated guest audio hardware.
Expanded audio, if desired, must be a documented optional device or future
profile. Normal operation must be possible without a host audio device.

## 19. Integrated development environment

An optional programming workspace: BASIC and assembly source editors, syntax
highlighting, line numbers, find/replace, undo/redo, open/save, validation,
guest program transfer, assembly execution, diagnostics, binary generation,
symbol tables, listing views, project folders, recent files. The host editor is
a convenience for writing guest programs, not a replacement for guest execution.
Provide explicit commands: send BASIC program, assemble, assemble and load,
assemble cartridge, build language ROM, export binary, run. Prefer ordinary text
source files.

## 20. Integrated debugger

A capable machine debugger: register inspection, status flags, current
instruction, disassembly, memory inspection/editing, breakpoints, read/write
watchpoints, step instruction/over, run until address, resume, reset, execution
trace, cycle and frame counters, stack inspection, interrupt status, device
inspection, bank selection, symbolic labels. Support both architectures, with
special attention to the 65C816's changing instruction widths and banked
addressing. Debugger inspection must not modify emulated devices (side-effect-free
inspection APIs); modifying actions must be explicit. Bound trace storage.

## 21. Machine snapshots and save states

Versioned machine-state persistence covering model, CPU registers and flags,
execution mode, RAM, screen, attributes, device registers, keyboard queue, disk
controller state, tape state, cartridge/ROM identity, frame and cycle counters,
deterministic random state. Explicit versioned format with validation and
compatibility information. Reject malformed or incompatible state safely. Saving
or restoring must not corrupt original media. Atomic save operations. Verify
save/restore/continue equivalence under deterministic conditions. Lightweight
snapshots and bounded rewind only after reliable save states.

## 22. Disk image engineering

Audit both disk formats and controllers: image sizes, sector boundaries,
directories, allocation maps, filenames, lengths, invalid sectors, corrupt
headers, duplicates, overlapping allocations, full disks, invalid commands,
truncated images, read-only media, save failures. Provide host-side tools:
create/format, inspect metadata, list directory, show allocation map,
extract/import, verify, duplicate, export, recover. The host-side image explorer
is distinct from guest filesystem operations. Inspection must not modify a
user-selected disk. Risky repairs default to read-only analysis with confirmation.

## 23. Tape image engineering

Review both tape implementations: loading, saving, validation, block parsing,
checksums, rewind, position reporting, record/playback, error recovery, invalid
input. A host-side cassette explorer may inspect recognizable blocks, reporting
metadata (name, length, position, checksum). Do not imply arbitrary bytes contain
recognizable structures. Preserve original tape contents on inspection.

## 24. Cartridge ecosystem

Review cartridge loading, startup and mapping. Provide management: catalogue,
metadata, compatibility, source inspection, assembly, validation, export,
mount/eject, cold boot, development templates. Develop additional original
demonstration cartridges after the infrastructure is validated. The catalogue
must not depend on internet access.

## 25. Data integrity and safe file operations

Treat user-created programs and media as valuable. Audit all filesystem
operations. Eliminate avoidable destructive behavior: partial writes, interrupted
saves, truncation, accidental overwrite, incorrect file type, wrong model,
malformed configuration, invalid paths, unbounded loading, resource exhaustion.
Use atomic replacement where appropriate; preserve the original if serialization
fails; optional backup before overwriting valuable media; visible filesystem
permission errors; no silently swallowed IO errors; clear dirty-media indication;
warnings before losing unsaved changes; honest durability claims.

## 26. Configuration and persistence

Platform-appropriate application data locations. Separate application
preferences, machine defaults, recent files, window geometry, user-selected ROMs,
user media, saved states and development projects. Documented, versioned
configuration format with safe loading and migration; malformed preferences must
not prevent startup; restoration of defaults. Avoid writing into the installation
directory unless in an explicit portable mode. Never store unnecessary personal
information.

## 27. Command-line interface

Consistent argument syntax, accurate help, version reporting, predictable exit
codes, clear errors, correct ordering, validated numeric ranges, missing-value
detection, unknown-option rejection, reliable relative paths, script-friendly
output, no unintended interactive prompts in batch mode. Machine-readable output
for selected commands where useful. Every documented command must actually work.

## 28. Headless operation and automation

Reliable scripted operation: booting either machine, typing BASIC, loading files,
running programs, capturing screen contents, inspecting results, assembling
ROMs/cartridges, inspecting media, executing regression scenarios. Deterministic
test fixtures. Avoid GUI dependencies for the headless emulator. Configurable
execution limits for untrusted or malfunctioning guest programs; return useful
failure information instead of hanging.

## 29. Performance engineering

Measure before optimizing. Repeatable benchmarks: CPU throughput, frame
execution, ROM assembly, cold startup, screen rendering, turbo execution, BASIC
execution, disk operations, snapshot serialization, idle consumption. Profile
actual bottlenecks. Avoid sacrificing correctness for throughput. Avoid unsafe
Rust unless genuinely necessary and justified (document invariants, add tests).
Do not publish invented performance claims; record hardware, configuration,
methodology and build profile.

## 30. Security and robustness

Local files remain untrusted inputs. Review assembly parsing, recursive includes,
ROM/cartridge/disk/tape/configuration/save-state parsing, debugger command
parsing, clipboard input, path handling, oversized and malformed data, and
resource consumption. Provide reasonable limits and controlled errors. Consider
fuzz testing for parsers and media formats. No guest access to arbitrary host
filesystem operations, shell commands or network services. Preserve the
guest/host trust boundary. Review dependency security and licensing.

## 31. Testing strategy

Expand the regression infrastructure across unit tests, CPU conformance,
memory-map, hardware-device, assembler, ROM boot, BASIC language, KERNAL service,
media integrity, snapshot, CLI, configuration, GUI layout/interaction,
documentation example, packaging smoke, malformed-input and performance
benchmark categories. Prefer tests that prove meaningful behavior. The existing
BASIC samples and their expected-output files are valuable fixtures — preserve
and expand them. Never weaken a failing test to obtain a green build; investigate
and correct only on documented evidence.

## 32. Software catalogue

A substantial collection of original software: games, mathematics, education,
business utilities, productivity, graphics, music, data processing, simulations,
tutorials, machine-language utilities, appropriate to both generations, using
real guest capabilities. Each example: source, purpose, compatible machine,
instructions, expected behavior, license/provenance, and deterministic expected
output fixtures where possible. Prefer sample disk images generated from source.

## 33. Educational environment

Progressive learning materials: booting, BASIC commands, variables, expressions,
loops, subroutines, arrays, strings, screen memory, binary/hex, memory maps,
assembly fundamentals, CPU registers, interrupts, KERNAL services, disk
structures, banked memory, cartridge programming, ROM development. Practical
exercises with working solutions. Distinguish introductory learning from
technical reference.

## 34. Documentation overhaul

Review every documentation file. Preserve the period-style owner's manuals.
Categorize: quickstart, everyday user guide, owner's manuals, BASIC dialect
reference, hardware technical reference, assembly programming guide, emulator
developer guide, debugger guide, media format specifications, configuration
reference, troubleshooting, development history, release notes. Remove stale
implementation claims; correct contradictions; verify commands and examples;
review hardware tables against source; maintain links; make terminology
consistent; retain fictional tone without obscuring technical information; add
actual screenshots (not fabrications); add diagrams where they help; document
known limitations plainly.

## 35. README and repository presentation

The README is the front door: what HB Computers is, why distinctive, what
machines it emulates, what a user can do, how to get started, where the manuals
are, how to develop software, and the license. Include genuine screenshots
(Kestrel and Kestrel 16 running BASIC, settings, representative programs,
debugger/workspace/media tools if implemented). Accurate CI badges, appropriate
topics, concise repository description. No marketing hype, fabricated popularity
or unverifiable benchmarks.

## 36. Portability

Excellent macOS and Linux support; evaluate Windows systematically. Remove
unnecessary platform assumptions (paths, separators, case sensitivity, config
directories, keyboard mappings, window behavior, audio, graphics backends,
packaging, shell scripts, permissions, executable discovery). Equivalent build
instructions for supported platforms. Do not claim unsupported platforms.
Headless functionality must be portable even when desktop packaging is not.

## 37. Distribution and installation

Self-contained distribution per OS, including all required runtime resources
(ROM sources or appropriate generated ROM assets): macOS application bundle,
Linux portable archive/AppImage, Windows portable archive/installer, separate
headless distribution. No compiler toolchain required merely to run a published
executable. ROM lookup must work after relocation. Package smoke tests in a clean
environment. Preserve licensing notices and produce checksums. Do not require
code-signing secrets for normal source builds; document signing/notarization
limitations honestly.

## 38. Continuous integration

Improve GitHub Actions: macOS, Linux, Windows (when supported), locked dependency
builds, unit/integration tests, documentation checks, formatting, clippy,
dependency review, package generation, package smoke tests, screenshot capture
where reliable. Bounded execution time; avoid redundant runs; do not disable
failing tests; investigate platform-specific failures; pin toolchains and
dependencies appropriately; no credentials or signing keys in the repository.

## 39. Maintainability

Meaningful naming, cohesive modules, clear ownership, appropriate comments,
documented invariants, consistent error handling, sensible abstractions, stable
interfaces, reproducible builds, clear test organization. Remove genuinely
obsolete code only after confirming it is unused. Archive useful development
history; do not delete it merely because it is old. Avoid elaborate enterprise
architecture; the code should remain accessible to experienced hobbyists.

## 40. Optional future extensibility

Evaluate the cost of supporting additional machines or optional hardware (more
profiles, RAM expansion, optional graphics/sound devices, additional storage
controllers, keyboard configurations). Do not invent a third machine until the
two existing machines are mature. Avoid a sprawling plugin architecture before
concrete requirements justify it. Do not compromise existing compatibility for
hypothetical futures.

## 41. Engineering execution order

- Stage A — Establish baseline: inspect, resolve integration issues, run tests,
  record defects and debt, reproducible builds.
- Stage B — Correctness and data integrity: CPU conformance, memory/device
  behavior, ROM/assembler correctness, BASIC correctness, safe file operations,
  error handling.
- Stage C — Architecture and usability: refactor oversized modules, frontend
  organization, machine control, configuration, media handling, keyboard/display.
- Stage D — Development capabilities: debugger, disassembler, memory/register
  inspection, source editor, assembly workflow, symbol integration, save states.
- Stage E — Software ecosystem: BASIC enhancements, programs, cartridges,
  educational material, development templates.
- Stage F — Documentation and distribution: verified manuals, screenshots,
  README, packaging, cross-platform CI, clean-install validation.
- Stage G — Final acceptance: full regression, conformance evidence, GUI checks,
  media integrity validation, package smoke tests, performance assessment,
  documentation audit, repository cleanliness, release readiness report.

Stages are logical priorities, not rigid barriers.

## 42. Git workflow

Focused, comprehensible commits. Keep work-in-progress synchronized to
appropriate GitHub branches. Do not force-push over maintainer work or rewrite
public history. Do not modify unrelated Honey Badger Universe repositories.
Review existing branches before creating redundant ones. Preserve the default
branch as coherent and buildable. Integrate completed work through the
established maintainer-approved workflow. Do not publish a release, create tags
or change the public version without explicit maintainer authorization.

## 43. Acceptance scenarios

New user, retrocomputing enthusiast, BASIC programmer, assembly programmer,
power user, automated tester, independent maintainer and distribution recipient
must each be able to accomplish their goals (launch and run BASIC without source;
explore and run media; develop/load/debug programs; control execution and
persist state safely; boot headlessly and inspect results; build, test and
understand from source; relocate and launch a packaged application).

## 44. Definition of done

A feature is not complete merely because it compiles. Completion requires
evidence of functional correctness, error handling, regression coverage,
compatibility, documentation, user-facing usability and maintainability. Do not
substitute placeholders, mark partial features complete, claim unexecuted tests,
claim unvalidated platforms or overstate CPU accuracy. Record limitations and
unresolved defects. If work stops, preserve precise implementation status and
actionable continuation instructions.

## 45. Reporting requirements

At the end of each substantive pass, update `IMPLEMENTATION_STATUS.md` (work
completed, files changed, architectural decisions, defects corrected, features
implemented, actual test commands and results, performance measurements, known
limitations, remaining work, relevant commits, CI status) and `NEXT_ACTIONS.md`
(concrete next tasks) — detailed enough for another engineering agent to resume.

## 46. Final instruction

Be ambitious but disciplined. Treat HB Computers as a genuine fictional computing
platform. Improve the actual engineering, user experience, guest programming
environments, development tools, documentation, reliability and discoverability.
Preserve the machines' personality, technical boundaries and open-source
philosophy. Investigate before assuming; test continuously; commit coherent
improvements; continue through the implementation stages rather than stopping
after an assessment.
