# Processor conformance matrix

This documents what the two CPU cores in `kestrel-machine` are verified to do,
and with what level of confidence. It distinguishes three levels the directive
requires:

- **Functional correctness** — does the instruction produce the right registers,
  memory and flags?
- **Instruction-level cycle accounting** — does the instruction report the right
  number of cycles?
- **Bus-level cycle accuracy** — does the instruction reproduce the bus activity
  cycle by cycle?

Neither core claims bus-level cycle accuracy. They execute one instruction per
`step` with a cycle count, and the machine uses those counts only for frame
pacing (`cycles_per_frame`) and timing. Instruction-level cycle accounting is
the strongest cycle claim made here.

## Evidence

- Both opcode tables were generated from the deleted C++ core's tables, so the
  opcode sets and base cycle counts are ported rather than hand-invented.
- `cpu6502.rs` carries ported unit tests for addressing, arithmetic, flags,
  decimal mode, the stack, interrupts and cycle counts.
- `cpu65816/tests.rs` carries the same for the 65816.
- The sample programs in `samples/` are typed at the machines and compared with
  their `.expected` files, exercising the cores end-to-end through the ROMs.
- The documented BASIC examples are re-run by `scripts/check_docs.py`.

No third-party CPU test-vector suite (for example the Klaus Dormann functional
tests or a legal 65C816 equivalent) is currently wired in; see `NEXT_ACTIONS.md`.

## MOS 6502 (`cpu6502.rs`)

| Area | Status | Notes |
| --- | --- | --- |
| Legal opcode coverage | Complete | 151 documented entries, asserted by `the_table_is_the_cpp_table`. |
| Undocumented opcodes | Policy: counted, not executed | `jam()` increments `illegal_opcodes` and stores the opcode; the PC is not advanced past the operand. A working ROM keeps the count at zero. This is a deliberate, documented policy, not accidental coverage. |
| Addressing modes | Verified | Accumulator, immediate, zero page, zero page X/Y, absolute, absolute X/Y, (zero page,X), (zero page),Y, indirect, relative; the zero-page pointer wrap and the `JMP ($xxFF)` page bug are tested. |
| Page-boundary behavior | Verified | Loads add one cycle on a page cross; stores do not; branches add one for taken and one more for a page cross. |
| Branch timing | Verified | Taken branch +1, taken branch crossing a page +2 total. |
| Stack behavior | Verified | Page one, wrapping 8-bit stack pointer; PHA/PLA, PHP/PLP, JSR/RTS, BRK/RTI round trips tested. |
| Decimal arithmetic | Verified | ADC and SBC adjust a nibble at a time. Z is taken from the binary result, V from the binary signs, C from the decimal result. SBC reports N/Z from the binary difference. |
| Processor flags | Verified | `set_zn` is the single place Z/N are set from a result; C/V are set by the arithmetic helpers; BIT, CMP/CPX/CPY, INC/DEC on memory covered. |
| Reset | Convention | Sets SP to `$FD` (the "counted down by three" value), I set, D clear, PC from `$FFFC`. The KERNAL sets its own stack pointer during boot. |
| IRQ / NMI | Verified | IRQ level-triggered and maskable; NMI edge-triggered and not maskable; both push the status with the B bit clear; BRK pushes with B set. |
| Interrupt return | Verified | RTI restores status (B cleared, unused bit set) and PC. |
| Indirect addressing quirks | Verified | `JMP (abs)` reproduces the same-page high-byte bug. |
| Read-modify-write semantics | Verified | INC/DEC/ASL/LSR/ROL/ROR read the byte, write it back and set Z/N; accumulator forms behave. |
| Cycle accounting | Corrected | RMW `abs,X`/`abs,Y` no longer adds a page-crossing penalty; it is fixed at seven cycles as the datasheet states (see commit `e8849a1`). Loads keep their penalty. |
| Illegal instruction handling | Controlled | Illegal opcodes are counted, not executed, and cannot write memory or jam the loop. |

## WDC 65C816 (`cpu65816.rs`)

| Area | Status | Notes |
| --- | --- | --- |
| Legal opcode coverage | Complete | All documented 65816 opcodes, asserted by the table tests. |
| Emulation / native modes | Verified | Emulation forces 8-bit accumulator and index registers, fixes the stack to page one, clears D, and keeps banks at zero. |
| Accumulator width (M) | Verified | Every read/write/push/pull/compare uses `m8()` to choose the width. |
| Index width (X) | Verified | Indexed instructions use `x8()`; `LDX/LDY/STX/STY/CPX/CPY` use the index width and never M. |
| Direct page (D) | Verified | A D whose low byte is not zero adds a cycle per access; zero-page wrap with 8-bit X is handled. |
| Data bank register (DBR) | Verified | Non-stack, non-direct-page addresses use `data_address`. |
| Program bank register (PBR) | Verified | `JMP` stays in the program bank; only `JML`/`JSL` set one. |
| Stack modes | Verified | 16-bit stack in native mode, page-one 8-bit stack in emulation. |
| Long addressing | Verified | Long, long,X, `JML`, `JSL`/`RTL`, `[dp]`, `[dp],Y`, `[abs]` present and bank-correct. |
| Bank boundary behavior | Verified | 16-bit reads/writes wrap within the bank; indirect-long reads cross banks. |
| Block moves (MVN/MVP) | Verified | Correct direction, bank-to-bank movement, X/Y update, C left at `$FFFF`. |
| Status register transitions | Verified | REP/SEP mask; XCE swaps carry and emulation; PLP/RTI restore widths. |
| Interrupt vector selection | Verified | Emulation vectors at `$FFFA`/`$FFFE`; native vectors at `$FFE4`–`$FFEE`. |
| Width restoration after interrupts | Verified | Hardware interrupts push P as-is (no forced width); software interrupts set bit 4 in the pushed copy only. |
| Decimal arithmetic | Verified | Nibble-at-a-time decimal adjust for both widths; C from the decimal result, V from the binary intermediate. |
| RMW page crossing | Verified | RMW `abs,X` is fixed at seven cycles; no page-crossing penalty (consistent with the corrected 6502 core). |
| WAI / STP | Verified | WAI idles one cycle per step until an interrupt; STP stops and charges one cycle per step. |
| WDM | Policy | Two-byte NOP with its signature byte read and discarded. |

## Known limitations

- Cycle accounting is instruction-level only. No bus-level cycle accuracy is
  claimed.
- The 6502's undocumented/illegal opcodes are not executed; programs that rely
  on them will not run. The shipped ROMs do not.
- No third-party conformance vectors are wired in yet; coverage currently comes
  from the ported C++ suites, the machine integration tests and the samples.
