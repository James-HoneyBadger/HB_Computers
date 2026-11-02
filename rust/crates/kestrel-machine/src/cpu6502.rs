// SPDX-License-Identifier: MIT
//
// The Kestrel's processor: a MOS 6502, cycle-counted.
//
// The machine's language is not a Rust interpreter: it is 6502 machine code
// assembled into the ROM in `roms/kestrel/`, and this is the core that executes
// it. Every `PRINT`, every `GOTO` and every error message on the tube comes out
// of a byte the ROM put there.
//
// `TABLE` is the opcode map, one entry per documented instruction, and it was
// generated from the C++ core's `kBaseCycles` and its `switch`, so the two trees
// cannot drift: an opcode is either in both or in neither, at the same cycle
// count. What the entry does *not* carry is the part that depends on the
// operands -- a page crossing on an indexed read, a branch that is taken, a
// branch that crosses a page -- which is added where it happens.
//
// Three things are worth knowing before reading further:
//
//   * flags. `set_zn` is the only place Z and N are set from a result; the
//     arithmetic helpers set C and V themselves, because their rules are not the
//     same for every instruction that touches them;
//   * decimal mode. ADC and SBC adjust a nibble at a time and report the flags
//     the chip reported: C from the decimal result, V from the binary one;
//   * undocumented opcodes. An opcode with no entry is counted and stepped over
//     rather than executed. A working ROM leaves the count at zero, so a test
//     that finds it non-zero has found a runaway.

/// The 64 KiB address space as the processor sees it.
pub trait Bus {
    fn read(&mut self, address: u16) -> u8;
    fn write(&mut self, address: u16, value: u8);
}

// Status register bits, named the way the data sheets name them.
pub const CARRY: u8 = 0x01;
pub const ZERO: u8 = 0x02;
pub const INTERRUPT_DISABLE: u8 = 0x04;
pub const DECIMAL: u8 = 0x08;
pub const BREAK: u8 = 0x10;
pub const UNUSED: u8 = 0x20;
pub const OVERFLOW: u8 = 0x40;
pub const NEGATIVE: u8 = 0x80;

/// What the processor is holding. A snapshot, for the monitor and tests.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Registers {
    pub a: u8,
    pub x: u8,
    pub y: u8,
    pub sp: u8,
    pub status: u8,
    pub pc: u16,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Mode {
    Imp,
    Acc,
    Imm,
    Zp,
    ZpX,
    ZpY,
    Abs,
    AbsX,
    AbsY,
    IndX,
    IndY,
    Ind,
    Rel,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Op {
    None,
    Adc, And, Asl, Bcc, Bcs, Beq, Bit, Bmi, Bne, Bpl, Brk, Bvc, Bvs,
    Clc, Cld, Cli, Clv, Cmp, Cpx, Cpy, Dec, Dex, Dey, Eor, Inc, Inx, Iny,
    Jmp, Jsr, Lda, Ldx, Ldy, Lsr, Nop, Ora, Pha, Php, Pla, Plp, Rol, Ror,
    Rti, Rts, Sbc, Sec, Sed, Sei, Sta, Stx, Sty, Tax, Tay, Tsx, Txa, Txs, Tya,
}

struct Entry {
    op: Op,
    mode: Mode,
    cycles: u8,
}

const NONE: Entry = Entry {
    op: Op::None,
    mode: Mode::Imp,
    cycles: 0,
};

#[rustfmt::skip]
static TABLE: [Entry; 256] = [
    Entry { op: Op::Brk, mode: Mode::Imp, cycles: 7 },
    Entry { op: Op::Ora, mode: Mode::IndX, cycles: 6 },
    NONE,
    NONE,
    NONE,
    Entry { op: Op::Ora, mode: Mode::Zp, cycles: 3 },
    Entry { op: Op::Asl, mode: Mode::Zp, cycles: 5 },
    NONE,
    Entry { op: Op::Php, mode: Mode::Imp, cycles: 3 },
    Entry { op: Op::Ora, mode: Mode::Imm, cycles: 2 },
    Entry { op: Op::Asl, mode: Mode::Acc, cycles: 2 },
    NONE,
    NONE,
    Entry { op: Op::Ora, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Asl, mode: Mode::Abs, cycles: 6 },
    NONE,
    Entry { op: Op::Bpl, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::Ora, mode: Mode::IndY, cycles: 5 },
    NONE,
    NONE,
    NONE,
    Entry { op: Op::Ora, mode: Mode::ZpX, cycles: 4 },
    Entry { op: Op::Asl, mode: Mode::ZpX, cycles: 6 },
    NONE,
    Entry { op: Op::Clc, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Ora, mode: Mode::AbsY, cycles: 4 },
    NONE,
    NONE,
    NONE,
    Entry { op: Op::Ora, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Asl, mode: Mode::AbsX, cycles: 7 },
    NONE,
    Entry { op: Op::Jsr, mode: Mode::Abs, cycles: 6 },
    Entry { op: Op::And, mode: Mode::IndX, cycles: 6 },
    NONE,
    NONE,
    Entry { op: Op::Bit, mode: Mode::Zp, cycles: 3 },
    Entry { op: Op::And, mode: Mode::Zp, cycles: 3 },
    Entry { op: Op::Rol, mode: Mode::Zp, cycles: 5 },
    NONE,
    Entry { op: Op::Plp, mode: Mode::Imp, cycles: 4 },
    Entry { op: Op::And, mode: Mode::Imm, cycles: 2 },
    Entry { op: Op::Rol, mode: Mode::Acc, cycles: 2 },
    NONE,
    Entry { op: Op::Bit, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::And, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Rol, mode: Mode::Abs, cycles: 6 },
    NONE,
    Entry { op: Op::Bmi, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::And, mode: Mode::IndY, cycles: 5 },
    NONE,
    NONE,
    NONE,
    Entry { op: Op::And, mode: Mode::ZpX, cycles: 4 },
    Entry { op: Op::Rol, mode: Mode::ZpX, cycles: 6 },
    NONE,
    Entry { op: Op::Sec, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::And, mode: Mode::AbsY, cycles: 4 },
    NONE,
    NONE,
    NONE,
    Entry { op: Op::And, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Rol, mode: Mode::AbsX, cycles: 7 },
    NONE,
    Entry { op: Op::Rti, mode: Mode::Imp, cycles: 6 },
    Entry { op: Op::Eor, mode: Mode::IndX, cycles: 6 },
    NONE,
    NONE,
    NONE,
    Entry { op: Op::Eor, mode: Mode::Zp, cycles: 3 },
    Entry { op: Op::Lsr, mode: Mode::Zp, cycles: 5 },
    NONE,
    Entry { op: Op::Pha, mode: Mode::Imp, cycles: 3 },
    Entry { op: Op::Eor, mode: Mode::Imm, cycles: 2 },
    Entry { op: Op::Lsr, mode: Mode::Acc, cycles: 2 },
    NONE,
    Entry { op: Op::Jmp, mode: Mode::Abs, cycles: 3 },
    Entry { op: Op::Eor, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Lsr, mode: Mode::Abs, cycles: 6 },
    NONE,
    Entry { op: Op::Bvc, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::Eor, mode: Mode::IndY, cycles: 5 },
    NONE,
    NONE,
    NONE,
    Entry { op: Op::Eor, mode: Mode::ZpX, cycles: 4 },
    Entry { op: Op::Lsr, mode: Mode::ZpX, cycles: 6 },
    NONE,
    Entry { op: Op::Cli, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Eor, mode: Mode::AbsY, cycles: 4 },
    NONE,
    NONE,
    NONE,
    Entry { op: Op::Eor, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Lsr, mode: Mode::AbsX, cycles: 7 },
    NONE,
    Entry { op: Op::Rts, mode: Mode::Imp, cycles: 6 },
    Entry { op: Op::Adc, mode: Mode::IndX, cycles: 6 },
    NONE,
    NONE,
    NONE,
    Entry { op: Op::Adc, mode: Mode::Zp, cycles: 3 },
    Entry { op: Op::Ror, mode: Mode::Zp, cycles: 5 },
    NONE,
    Entry { op: Op::Pla, mode: Mode::Imp, cycles: 4 },
    Entry { op: Op::Adc, mode: Mode::Imm, cycles: 2 },
    Entry { op: Op::Ror, mode: Mode::Acc, cycles: 2 },
    NONE,
    Entry { op: Op::Jmp, mode: Mode::Ind, cycles: 5 },
    Entry { op: Op::Adc, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Ror, mode: Mode::Abs, cycles: 6 },
    NONE,
    Entry { op: Op::Bvs, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::Adc, mode: Mode::IndY, cycles: 5 },
    NONE,
    NONE,
    NONE,
    Entry { op: Op::Adc, mode: Mode::ZpX, cycles: 4 },
    Entry { op: Op::Ror, mode: Mode::ZpX, cycles: 6 },
    NONE,
    Entry { op: Op::Sei, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Adc, mode: Mode::AbsY, cycles: 4 },
    NONE,
    NONE,
    NONE,
    Entry { op: Op::Adc, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Ror, mode: Mode::AbsX, cycles: 7 },
    NONE,
    NONE,
    Entry { op: Op::Sta, mode: Mode::IndX, cycles: 6 },
    NONE,
    NONE,
    Entry { op: Op::Sty, mode: Mode::Zp, cycles: 3 },
    Entry { op: Op::Sta, mode: Mode::Zp, cycles: 3 },
    Entry { op: Op::Stx, mode: Mode::Zp, cycles: 3 },
    NONE,
    Entry { op: Op::Dey, mode: Mode::Imp, cycles: 2 },
    NONE,
    Entry { op: Op::Txa, mode: Mode::Imp, cycles: 2 },
    NONE,
    Entry { op: Op::Sty, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Sta, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Stx, mode: Mode::Abs, cycles: 4 },
    NONE,
    Entry { op: Op::Bcc, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::Sta, mode: Mode::IndY, cycles: 6 },
    NONE,
    NONE,
    Entry { op: Op::Sty, mode: Mode::ZpX, cycles: 4 },
    Entry { op: Op::Sta, mode: Mode::ZpX, cycles: 4 },
    Entry { op: Op::Stx, mode: Mode::ZpY, cycles: 4 },
    NONE,
    Entry { op: Op::Tya, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Sta, mode: Mode::AbsY, cycles: 5 },
    Entry { op: Op::Txs, mode: Mode::Imp, cycles: 2 },
    NONE,
    NONE,
    Entry { op: Op::Sta, mode: Mode::AbsX, cycles: 5 },
    NONE,
    NONE,
    Entry { op: Op::Ldy, mode: Mode::Imm, cycles: 2 },
    Entry { op: Op::Lda, mode: Mode::IndX, cycles: 6 },
    Entry { op: Op::Ldx, mode: Mode::Imm, cycles: 2 },
    NONE,
    Entry { op: Op::Ldy, mode: Mode::Zp, cycles: 3 },
    Entry { op: Op::Lda, mode: Mode::Zp, cycles: 3 },
    Entry { op: Op::Ldx, mode: Mode::Zp, cycles: 3 },
    NONE,
    Entry { op: Op::Tay, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Lda, mode: Mode::Imm, cycles: 2 },
    Entry { op: Op::Tax, mode: Mode::Imp, cycles: 2 },
    NONE,
    Entry { op: Op::Ldy, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Lda, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Ldx, mode: Mode::Abs, cycles: 4 },
    NONE,
    Entry { op: Op::Bcs, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::Lda, mode: Mode::IndY, cycles: 5 },
    NONE,
    NONE,
    Entry { op: Op::Ldy, mode: Mode::ZpX, cycles: 4 },
    Entry { op: Op::Lda, mode: Mode::ZpX, cycles: 4 },
    Entry { op: Op::Ldx, mode: Mode::ZpY, cycles: 4 },
    NONE,
    Entry { op: Op::Clv, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Lda, mode: Mode::AbsY, cycles: 4 },
    Entry { op: Op::Tsx, mode: Mode::Imp, cycles: 2 },
    NONE,
    Entry { op: Op::Ldy, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Lda, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Ldx, mode: Mode::AbsY, cycles: 4 },
    NONE,
    Entry { op: Op::Cpy, mode: Mode::Imm, cycles: 2 },
    Entry { op: Op::Cmp, mode: Mode::IndX, cycles: 6 },
    NONE,
    NONE,
    Entry { op: Op::Cpy, mode: Mode::Zp, cycles: 3 },
    Entry { op: Op::Cmp, mode: Mode::Zp, cycles: 3 },
    Entry { op: Op::Dec, mode: Mode::Zp, cycles: 5 },
    NONE,
    Entry { op: Op::Iny, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Cmp, mode: Mode::Imm, cycles: 2 },
    Entry { op: Op::Dex, mode: Mode::Imp, cycles: 2 },
    NONE,
    Entry { op: Op::Cpy, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Cmp, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Dec, mode: Mode::Abs, cycles: 6 },
    NONE,
    Entry { op: Op::Bne, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::Cmp, mode: Mode::IndY, cycles: 5 },
    NONE,
    NONE,
    NONE,
    Entry { op: Op::Cmp, mode: Mode::ZpX, cycles: 4 },
    Entry { op: Op::Dec, mode: Mode::ZpX, cycles: 6 },
    NONE,
    Entry { op: Op::Cld, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Cmp, mode: Mode::AbsY, cycles: 4 },
    NONE,
    NONE,
    NONE,
    Entry { op: Op::Cmp, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Dec, mode: Mode::AbsX, cycles: 7 },
    NONE,
    Entry { op: Op::Cpx, mode: Mode::Imm, cycles: 2 },
    Entry { op: Op::Sbc, mode: Mode::IndX, cycles: 6 },
    NONE,
    NONE,
    Entry { op: Op::Cpx, mode: Mode::Zp, cycles: 3 },
    Entry { op: Op::Sbc, mode: Mode::Zp, cycles: 3 },
    Entry { op: Op::Inc, mode: Mode::Zp, cycles: 5 },
    NONE,
    Entry { op: Op::Inx, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Sbc, mode: Mode::Imm, cycles: 2 },
    Entry { op: Op::Nop, mode: Mode::Imp, cycles: 2 },
    NONE,
    Entry { op: Op::Cpx, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Sbc, mode: Mode::Abs, cycles: 4 },
    Entry { op: Op::Inc, mode: Mode::Abs, cycles: 6 },
    NONE,
    Entry { op: Op::Beq, mode: Mode::Rel, cycles: 2 },
    Entry { op: Op::Sbc, mode: Mode::IndY, cycles: 5 },
    NONE,
    NONE,
    NONE,
    Entry { op: Op::Sbc, mode: Mode::ZpX, cycles: 4 },
    Entry { op: Op::Inc, mode: Mode::ZpX, cycles: 6 },
    NONE,
    Entry { op: Op::Sed, mode: Mode::Imp, cycles: 2 },
    Entry { op: Op::Sbc, mode: Mode::AbsY, cycles: 4 },
    NONE,
    NONE,
    NONE,
    Entry { op: Op::Sbc, mode: Mode::AbsX, cycles: 4 },
    Entry { op: Op::Inc, mode: Mode::AbsX, cycles: 7 },
    NONE,];

pub struct Cpu6502 {
    a: u8,
    x: u8,
    y: u8,
    sp: u8,
    status: u8,
    pc: u16,
    irq_line: bool,
    nmi_pending: bool,
    cycles_total: u64,
    instructions: u64,
    illegal_opcodes: u64,
    last_illegal_opcode: u8,
}

impl Default for Cpu6502 {
    fn default() -> Self {
        Self::new()
    }
}

impl Cpu6502 {
    /// The state a real chip presents after a reset: the stack pointer has
    /// counted down by three, interrupts are masked and decimal mode is off.
    /// Everything else is zero, which is why a ROM cannot rely on any register.
    pub fn new() -> Self {
        Self {
            a: 0,
            x: 0,
            y: 0,
            sp: 0xFD,
            status: INTERRUPT_DISABLE | UNUSED,
            pc: 0,
            irq_line: false,
            nmi_pending: false,
            cycles_total: 0,
            instructions: 0,
            illegal_opcodes: 0,
            last_illegal_opcode: 0,
        }
    }

    pub fn reset(&mut self, bus: &mut dyn Bus) {
        self.a = 0;
        self.x = 0;
        self.y = 0;
        self.sp = 0xFD;
        self.status = INTERRUPT_DISABLE | UNUSED;
        self.pc = self.read_word(bus, 0xFFFC);
        self.irq_line = false;
        self.nmi_pending = false;
    }

    pub fn registers(&self) -> Registers {
        Registers {
            a: self.a,
            x: self.x,
            y: self.y,
            sp: self.sp,
            status: self.status,
            pc: self.pc,
        }
    }

    pub fn set_registers(&mut self, registers: &Registers) {
        self.a = registers.a;
        self.x = registers.x;
        self.y = registers.y;
        self.sp = registers.sp;
        self.status = registers.status;
        self.pc = registers.pc;
    }

    /// The maskable interrupt line. Level-triggered, as on the real chip: it
    /// stays high until the device that raised it is serviced.
    pub fn set_irq_line(&mut self, level: bool) {
        self.irq_line = level;
    }

    pub fn irq_line(&self) -> bool {
        self.irq_line
    }

    /// A non-maskable interrupt (the BREAK key). Edge-triggered: one request is
    /// one interrupt, however long it waits.
    pub fn request_nmi(&mut self) {
        self.nmi_pending = true;
    }

    /// Executes one instruction, or services a pending interrupt. Returns the
    /// cycles it took.
    pub fn step(&mut self, bus: &mut dyn Bus) -> u32 {
        // An interrupt is taken before the opcode is fetched, which is the order
        // the chip resolves them in, and costs the seven cycles of a BRK.
        if self.nmi_pending {
            self.nmi_pending = false;
            self.interrupt(bus, 0xFFFA);
            self.instructions += 1;
            self.cycles_total += 7;
            return 7;
        }
        if self.irq_line && !self.flag(INTERRUPT_DISABLE) {
            self.interrupt(bus, 0xFFFE);
            self.instructions += 1;
            self.cycles_total += 7;
            return 7;
        }

        let opcode = self.fetch(bus);
        let entry = &TABLE[opcode as usize];
        let (op, mode) = (entry.op, entry.mode);
        let mut cycles = i32::from(entry.cycles);
        self.instructions += 1;

        match op {
            Op::None => self.jam(opcode),
            Op::Adc => {
                let value = self.paid(bus, mode, &mut cycles);
                self.adc(value);
            }
            Op::And => {
                let value = self.paid(bus, mode, &mut cycles);
                self.a &= value;
                self.set_zn(self.a);
            }
            Op::Asl | Op::Lsr | Op::Rol | Op::Ror => {
                if mode == Mode::Acc {
                    let value = match op {
                        Op::Asl => self.asl(self.a),
                        Op::Lsr => self.lsr(self.a),
                        Op::Rol => self.rol(self.a),
                        _ => self.ror(self.a),
                    };
                    self.a = value;
                } else {
                    let (address, crossed) = self.address(bus, mode);
                    if crossed {
                        cycles += 1;
                    }
                    let value = bus.read(address);
                    let value = match op {
                        Op::Asl => self.asl(value),
                        Op::Lsr => self.lsr(value),
                        Op::Rol => self.rol(value),
                        _ => self.ror(value),
                    };
                    bus.write(address, value);
                }
            }
            Op::Bcc => self.branch(bus, !self.flag(CARRY), &mut cycles),
            Op::Bcs => self.branch(bus, self.flag(CARRY), &mut cycles),
            Op::Beq => self.branch(bus, self.flag(ZERO), &mut cycles),
            Op::Bmi => self.branch(bus, self.flag(NEGATIVE), &mut cycles),
            Op::Bne => self.branch(bus, !self.flag(ZERO), &mut cycles),
            Op::Bpl => self.branch(bus, !self.flag(NEGATIVE), &mut cycles),
            Op::Bvc => self.branch(bus, !self.flag(OVERFLOW), &mut cycles),
            Op::Bvs => self.branch(bus, self.flag(OVERFLOW), &mut cycles),
            Op::Bit => {
                let (address, _) = self.address(bus, mode);
                let value = bus.read(address);
                self.set_flag(ZERO, self.a & value == 0);
                self.set_flag(NEGATIVE, value & 0x80 != 0);
                self.set_flag(OVERFLOW, value & 0x40 != 0);
            }
            Op::Brk => {
                // The break flag is set in the pushed copy only: the register the
                // program sees is not what the interrupt returns to.
                let next = self.pc.wrapping_add(1);
                self.push(bus, (next >> 8) as u8);
                self.push(bus, next as u8);
                self.push(bus, self.status | BREAK | UNUSED);
                self.set_flag(INTERRUPT_DISABLE, true);
                self.pc = self.read_word(bus, 0xFFFE);
            }
            Op::Clc => self.set_flag(CARRY, false),
            Op::Cld => self.set_flag(DECIMAL, false),
            Op::Cli => self.set_flag(INTERRUPT_DISABLE, false),
            Op::Clv => self.set_flag(OVERFLOW, false),
            Op::Cmp => {
                let value = self.paid(bus, mode, &mut cycles);
                self.compare(self.a, value);
            }
            Op::Cpx => {
                let value = self.paid(bus, mode, &mut cycles);
                self.compare(self.x, value);
            }
            Op::Cpy => {
                let value = self.paid(bus, mode, &mut cycles);
                self.compare(self.y, value);
            }
            Op::Dec => {
                let (address, crossed) = self.address(bus, mode);
                if crossed {
                    cycles += 1;
                }
                let value = bus.read(address).wrapping_sub(1);
                bus.write(address, value);
                self.set_zn(value);
            }
            Op::Dex => {
                self.x = self.x.wrapping_sub(1);
                self.set_zn(self.x);
            }
            Op::Dey => {
                self.y = self.y.wrapping_sub(1);
                self.set_zn(self.y);
            }
            Op::Eor => {
                let value = self.paid(bus, mode, &mut cycles);
                self.a ^= value;
                self.set_zn(self.a);
            }
            Op::Inc => {
                let (address, crossed) = self.address(bus, mode);
                if crossed {
                    cycles += 1;
                }
                let value = bus.read(address).wrapping_add(1);
                bus.write(address, value);
                self.set_zn(value);
            }
            Op::Inx => {
                self.x = self.x.wrapping_add(1);
                self.set_zn(self.x);
            }
            Op::Iny => {
                self.y = self.y.wrapping_add(1);
                self.set_zn(self.y);
            }
            Op::Jmp => {
                let pointer = self.fetch_word(bus);
                self.pc = if mode == Mode::Ind {
                    self.read_word_bug(bus, pointer)
                } else {
                    pointer
                };
            }
            Op::Jsr => {
                let low = self.fetch(bus);
                let high = self.fetch(bus);
                let return_to = self.pc.wrapping_sub(1);
                self.push(bus, (return_to >> 8) as u8);
                self.push(bus, return_to as u8);
                self.pc = u16::from(low) | (u16::from(high) << 8);
            }
            Op::Lda => {
                let value = self.paid(bus, mode, &mut cycles);
                self.a = value;
                self.set_zn(self.a);
            }
            Op::Ldx => {
                let value = self.paid(bus, mode, &mut cycles);
                self.x = value;
                self.set_zn(self.x);
            }
            Op::Ldy => {
                let value = self.paid(bus, mode, &mut cycles);
                self.y = value;
                self.set_zn(self.y);
            }
            Op::Nop => {}
            Op::Ora => {
                let value = self.paid(bus, mode, &mut cycles);
                self.a |= value;
                self.set_zn(self.a);
            }
            Op::Pha => self.push(bus, self.a),
            Op::Php => self.push(bus, self.status | BREAK | UNUSED),
            Op::Pla => {
                self.a = self.pull(bus);
                self.set_zn(self.a);
            }
            Op::Plp => self.status = (self.pull(bus) & !BREAK) | UNUSED,
            Op::Rti => {
                self.status = (self.pull(bus) & !BREAK) | UNUSED;
                let low = u16::from(self.pull(bus));
                let high = u16::from(self.pull(bus));
                self.pc = low | (high << 8);
            }
            Op::Rts => {
                let low = u16::from(self.pull(bus));
                let high = u16::from(self.pull(bus));
                self.pc = (low | (high << 8)).wrapping_add(1);
            }
            Op::Sbc => {
                let value = self.paid(bus, mode, &mut cycles);
                self.sbc(value);
            }
            Op::Sec => self.set_flag(CARRY, true),
            Op::Sed => self.set_flag(DECIMAL, true),
            Op::Sei => self.set_flag(INTERRUPT_DISABLE, true),
            Op::Sta => {
                let (address, _) = self.address(bus, mode);
                bus.write(address, self.a);
            }
            Op::Stx => {
                let (address, _) = self.address(bus, mode);
                bus.write(address, self.x);
            }
            Op::Sty => {
                let (address, _) = self.address(bus, mode);
                bus.write(address, self.y);
            }
            Op::Tax => {
                self.x = self.a;
                self.set_zn(self.x);
            }
            Op::Tay => {
                self.y = self.a;
                self.set_zn(self.y);
            }
            Op::Tsx => {
                self.x = self.sp;
                self.set_zn(self.x);
            }
            Op::Txa => {
                self.a = self.x;
                self.set_zn(self.a);
            }
            Op::Txs => self.sp = self.x,
            Op::Tya => {
                self.a = self.y;
                self.set_zn(self.a);
            }
        }

        self.cycles_total += cycles.max(0) as u64;
        cycles.max(0) as u32
    }

    pub fn cycle_count(&self) -> u64 {
        self.cycles_total
    }

    pub fn instruction_count(&self) -> u64 {
        self.instructions
    }

    /// How many times the processor fetched an undocumented opcode. A working
    /// ROM leaves this at zero.
    pub fn illegal_opcodes(&self) -> u64 {
        self.illegal_opcodes
    }

    pub fn last_illegal_opcode(&self) -> u8 {
        self.last_illegal_opcode
    }

    fn fetch(&mut self, bus: &mut dyn Bus) -> u8 {
        let byte = bus.read(self.pc);
        self.pc = self.pc.wrapping_add(1);
        byte
    }

    fn fetch_word(&mut self, bus: &mut dyn Bus) -> u16 {
        let low = u16::from(self.fetch(bus));
        let high = u16::from(self.fetch(bus));
        low | (high << 8)
    }

    fn read_word(&self, bus: &mut dyn Bus, address: u16) -> u16 {
        let low = u16::from(bus.read(address));
        let high = u16::from(bus.read(address.wrapping_add(1)));
        low | (high << 8)
    }

    /// The indirect jump's famous fault: both halves of the pointer come from the
    /// same page, so `JMP ($02FF)` reads its high byte from `$0200`. It is a bug
    /// on the chip, and a machine that emulated it away would not run the code
    /// written around it.
    fn read_word_bug(&self, bus: &mut dyn Bus, address: u16) -> u16 {
        let low = u16::from(bus.read(address));
        let next = (address & 0xFF00) | (address.wrapping_add(1) & 0x00FF);
        let high = u16::from(bus.read(next));
        low | (high << 8)
    }

    /// The operand's address, and whether the index carried out of the base's
    /// page. Only the indexed modes can.
    fn address(&mut self, bus: &mut dyn Bus, mode: Mode) -> (u16, bool) {
        match mode {
            Mode::Zp => (u16::from(self.fetch(bus)), false),
            Mode::ZpX => (
                u16::from(self.fetch(bus).wrapping_add(self.x)),
                false,
            ),
            Mode::ZpY => (
                u16::from(self.fetch(bus).wrapping_add(self.y)),
                false,
            ),
            Mode::Abs => (self.fetch_word(bus), false),
            Mode::AbsX => {
                let base = self.fetch_word(bus);
                let address = base.wrapping_add(u16::from(self.x));
                (address, (base & 0xFF00) != (address & 0xFF00))
            }
            Mode::AbsY => {
                let base = self.fetch_word(bus);
                let address = base.wrapping_add(u16::from(self.y));
                (address, (base & 0xFF00) != (address & 0xFF00))
            }
            Mode::IndX => {
                let pointer = self.fetch(bus).wrapping_add(self.x);
                let low = u16::from(bus.read(u16::from(pointer)));
                let high = u16::from(bus.read(u16::from(pointer.wrapping_add(1))));
                (low | (high << 8), false)
            }
            Mode::IndY => {
                let pointer = self.fetch(bus);
                let low = u16::from(bus.read(u16::from(pointer)));
                let high = u16::from(bus.read(u16::from(pointer.wrapping_add(1))));
                let base = low | (high << 8);
                let address = base.wrapping_add(u16::from(self.y));
                (address, (base & 0xFF00) != (address & 0xFF00))
            }
            _ => (0, false),
        }
    }

    /// The operand's value, with the page-crossing penalty added for the
    /// instructions that pay it. The C++ core charges it to the read-modify-write
    /// instructions too, which the datasheet does not; the port keeps the machine
    /// it is a port of, so the pacing is the same.
    fn paid(&mut self, bus: &mut dyn Bus, mode: Mode, cycles: &mut i32) -> u8 {
        if mode == Mode::Imm {
            return self.fetch(bus);
        }
        let (address, crossed) = self.address(bus, mode);
        if crossed {
            *cycles += 1;
        }
        bus.read(address)
    }

    fn branch(&mut self, bus: &mut dyn Bus, taken: bool, cycles: &mut i32) {
        let offset = self.fetch(bus) as i8;
        if !taken {
            return;
        }
        let from = self.pc;
        self.pc = self.pc.wrapping_add(offset as u16);
        *cycles += 1;
        if (from & 0xFF00) != (self.pc & 0xFF00) {
            *cycles += 1;
        }
    }

    /// The stack lives in page one and wraps inside it: sixteen bits would be a
    /// memory the chip does not have.
    fn push(&mut self, bus: &mut dyn Bus, value: u8) {
        bus.write(0x0100 | u16::from(self.sp), value);
        self.sp = self.sp.wrapping_sub(1);
    }

    fn pull(&mut self, bus: &mut dyn Bus) -> u8 {
        self.sp = self.sp.wrapping_add(1);
        bus.read(0x0100 | u16::from(self.sp))
    }

    fn flag(&self, bit: u8) -> bool {
        self.status & bit != 0
    }

    fn set_flag(&mut self, bit: u8, on: bool) {
        if on {
            self.status |= bit;
        } else {
            self.status &= !bit;
        }
    }

    fn set_zn(&mut self, value: u8) {
        self.set_flag(ZERO, value == 0);
        self.set_flag(NEGATIVE, value & 0x80 != 0);
    }

    /// CMP, CPX and CPY: the subtraction that is only kept in the flags. Carry is
    /// set when the register is at least the operand, which is the borrow
    /// inverted.
    fn compare(&mut self, register: u8, value: u8) {
        self.set_flag(CARRY, register >= value);
        self.set_zn(register.wrapping_sub(value));
    }

    /// ADC. In binary the flags are the ones every reader expects. In decimal
    /// mode the two nibbles are adjusted separately, and the result is not the
    /// binary sum: Z is taken from the binary sum, N from the decimal digit that
    /// came out, and V from the binary signs, which is what the chip does and
    /// what a program that uses decimal mode twice in a row will notice.
    fn adc(&mut self, value: u8) {
        let carry = i32::from(self.flag(CARRY));
        if self.flag(DECIMAL) {
            let mut low = i32::from(self.a & 0x0F) + i32::from(value & 0x0F) + carry;
            if low > 9 {
                low += 6;
            }
            let mut high = i32::from(self.a >> 4) + i32::from(value >> 4) + i32::from(low > 0x0F);
            let binary = u8::wrapping_add(u8::wrapping_add(self.a, value), carry as u8);
            self.set_flag(ZERO, binary == 0);
            self.set_flag(NEGATIVE, high & 0x08 != 0);
            self.set_flag(
                OVERFLOW,
                (!(self.a ^ value) & (self.a ^ ((high as u8) << 4)) & 0x80) != 0,
            );
            if high > 9 {
                high += 6;
            }
            self.set_flag(CARRY, high > 0x0F);
            self.a = (((high << 4) | (low & 0x0F)) & 0xFF) as u8;
            return;
        }
        let sum = i32::from(self.a) + i32::from(value) + carry;
        self.set_flag(CARRY, sum > 0xFF);
        self.set_flag(
            OVERFLOW,
            (!(self.a ^ value) & (self.a ^ (sum as u8)) & 0x80) != 0,
        );
        self.a = sum as u8;
        self.set_zn(self.a);
    }

    /// SBC, which is ADC with the operand inverted, but is written out rather
    /// than composed: in decimal mode the borrow runs between the nibbles and a
    /// composed version gets the high digit wrong whenever the low one borrowed.
    fn sbc(&mut self, value: u8) {
        let borrow = i32::from(!self.flag(CARRY));
        let difference = i32::from(self.a) - i32::from(value) - borrow;
        if self.flag(DECIMAL) {
            let low = i32::from(self.a & 0x0F) - i32::from(value & 0x0F) - borrow;
            let mut high = i32::from(self.a >> 4) - i32::from(value >> 4);
            if low < 0 {
                high -= 1;
            }
            if high < 0 {
                high -= 6;
            }
            let adjusted_low = if low < 0 { (low - 6) & 0x0F } else { low & 0x0F };
            self.set_flag(CARRY, difference >= 0);
            self.set_flag(
                OVERFLOW,
                ((self.a ^ value) & (self.a ^ (difference as u8)) & 0x80) != 0,
            );
            self.a = (((high << 4) | adjusted_low) & 0xFF) as u8;
            // The 6502 reports N and Z from the binary subtraction even in
            // decimal mode; only the carry comes from the decimal digit.
            self.set_zn(difference as u8);
            return;
        }
        self.set_flag(CARRY, difference >= 0);
        self.set_flag(
            OVERFLOW,
            ((self.a ^ value) & (self.a ^ (difference as u8)) & 0x80) != 0,
        );
        self.a = difference as u8;
        self.set_zn(self.a);
    }

    fn asl(&mut self, value: u8) -> u8 {
        self.set_flag(CARRY, value & 0x80 != 0);
        let value = value << 1;
        self.set_zn(value);
        value
    }

    fn lsr(&mut self, value: u8) -> u8 {
        self.set_flag(CARRY, value & 0x01 != 0);
        let value = value >> 1;
        self.set_zn(value);
        value
    }

    fn rol(&mut self, value: u8) -> u8 {
        let carry = self.flag(CARRY);
        self.set_flag(CARRY, value & 0x80 != 0);
        let value = (value << 1) | u8::from(carry);
        self.set_zn(value);
        value
    }

    fn ror(&mut self, value: u8) -> u8 {
        let carry = self.flag(CARRY);
        self.set_flag(CARRY, value & 0x01 != 0);
        let value = (value >> 1) | if carry { 0x80 } else { 0 };
        self.set_zn(value);
        value
    }

    fn interrupt(&mut self, bus: &mut dyn Bus, vector: u16) {
        self.push(bus, (self.pc >> 8) as u8);
        self.push(bus, self.pc as u8);
        self.push(bus, (self.status & !BREAK) | UNUSED);
        self.set_flag(INTERRUPT_DISABLE, true);
        self.pc = self.read_word(bus, vector);
    }

    fn jam(&mut self, opcode: u8) {
        self.illegal_opcodes += 1;
        self.last_illegal_opcode = opcode;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 64 KiB of plain memory: enough to run a program and read back what it did.
    struct Ram {
        memory: Vec<u8>,
    }

    impl Ram {
        fn new() -> Self {
            Self {
                memory: vec![0; 0x1_0000],
            }
        }

        fn load(&mut self, at: u16, bytes: &[u8]) {
            let mut address = at;
            for byte in bytes {
                self.memory[address as usize] = *byte;
                address = address.wrapping_add(1);
            }
        }

        fn poke(&mut self, address: u16, value: u8) {
            self.memory[address as usize] = value;
        }

        fn peek(&self, address: u16) -> u8 {
            self.memory[address as usize]
        }
    }

    impl Bus for Ram {
        fn read(&mut self, address: u16) -> u8 {
            self.memory[address as usize]
        }

        fn write(&mut self, address: u16, value: u8) {
            self.memory[address as usize] = value;
        }
    }

    /// Runs one instruction from `opcode` and the bytes after it.
    struct Run {
        ram: Ram,
        cpu: Cpu6502,
    }

    impl Run {
        fn new(origin: u16, program: &[u8]) -> Self {
            let mut ram = Ram::new();
            ram.load(origin, program);
            let mut cpu = Cpu6502::new();
            let mut registers = cpu.registers();
            registers.pc = origin;
            registers.sp = 0xFD;
            registers.status = 0x24;
            cpu.set_registers(&registers);
            Self { ram, cpu }
        }

        fn step(&mut self) -> u32 {
            self.cpu.step(&mut self.ram)
        }

        fn regs(&self) -> Registers {
            self.cpu.registers()
        }

        fn set(&mut self, f: impl FnOnce(&mut Registers)) {
            let mut registers = self.cpu.registers();
            f(&mut registers);
            self.cpu.set_registers(&registers);
        }
    }

    #[test]
    fn the_table_is_the_cpp_table() {
        let documented = TABLE.iter().filter(|e| e.op != Op::None).count();
        assert_eq!(documented, 151, "the C++ switch has a hundred and fifty-one");
        // Every entry the C++ has, at the cycle count it has.
        assert_eq!(TABLE[0x00].cycles, 7, "BRK");
        assert_eq!(TABLE[0xA9].cycles, 2, "LDA #");
        assert_eq!(TABLE[0xBD].cycles, 4, "LDA abs,X");
        assert_eq!(TABLE[0x9D].cycles, 5, "STA abs,X");
        assert_eq!(TABLE[0x60].cycles, 6, "RTS");
        assert_eq!(TABLE[0x1E].cycles, 7, "ASL abs,X");
    }

    #[test]
    fn addressing_modes() {
        let mut run = Run::new(0x0200, &[0xA9, 0x2A]);
        run.step();
        assert_eq!(run.regs().a, 0x2A);
        assert_eq!(run.regs().pc, 0x0202);

        let mut run = Run::new(0x0200, &[0xA5, 0x10]);
        run.ram.poke(0x0010, 0x5A);
        run.step();
        assert_eq!(run.regs().a, 0x5A);

        let mut run = Run::new(0x0200, &[0xB5, 0x10]);
        run.ram.poke(0x0015, 0x77);
        run.set(|r| r.x = 5);
        run.step();
        assert_eq!(run.regs().a, 0x77);

        let mut run = Run::new(0x0200, &[0xB6, 0x10]);
        run.ram.poke(0x0013, 0x66);
        run.set(|r| r.y = 3);
        run.step();
        assert_eq!(run.regs().x, 0x66);

        let mut run = Run::new(0x0200, &[0xAD, 0x00, 0x40]);
        run.ram.poke(0x4000, 0x99);
        run.step();
        assert_eq!(run.regs().a, 0x99);

        let mut run = Run::new(0x0200, &[0xBD, 0x00, 0x40]);
        run.ram.poke(0x4002, 0x88);
        run.set(|r| r.x = 2);
        run.step();
        assert_eq!(run.regs().a, 0x88);

        let mut run = Run::new(0x0200, &[0xB9, 0x00, 0x40]);
        run.ram.poke(0x4003, 0x44);
        run.set(|r| r.y = 3);
        run.step();
        assert_eq!(run.regs().a, 0x44);

        let mut run = Run::new(0x0200, &[0xA1, 0x20]);
        run.ram.poke(0x0020, 0x00);
        run.ram.poke(0x0021, 0x30);
        run.ram.poke(0x3000, 0x11);
        run.step();
        assert_eq!(run.regs().a, 0x11);

        let mut run = Run::new(0x0200, &[0xB1, 0x20]);
        run.ram.poke(0x0020, 0xF0);
        run.ram.poke(0x0021, 0x30);
        run.ram.poke(0x30F1, 0x22);
        run.set(|r| r.y = 1);
        run.step();
        assert_eq!(run.regs().a, 0x22);

        // The zero page pointer wraps: $FF reads its high byte from $00.
        let mut run = Run::new(0x0200, &[0xB1, 0xFF]);
        run.ram.poke(0x00FF, 0x00);
        run.ram.poke(0x0000, 0x40);
        run.ram.poke(0x0100, 0x99);
        run.ram.poke(0x4000, 0xAB);
        run.step();
        assert_eq!(run.regs().a, 0xAB);
    }

    #[test]
    fn arithmetic_and_flags() {
        let mut run = Run::new(0x0200, &[0xA9, 0x7F, 0x69, 0x01]);
        run.step();
        run.step();
        let r = run.regs();
        assert_eq!(r.a, 0x80);
        assert_ne!(r.status & OVERFLOW, 0);
        assert_ne!(r.status & NEGATIVE, 0);
        assert_eq!(r.status & CARRY, 0);

        let mut run = Run::new(0x0200, &[0x18, 0xA9, 0xFF, 0x69, 0x02]);
        run.step();
        run.step();
        run.step();
        let r = run.regs();
        assert_eq!(r.a, 0x01);
        assert_ne!(r.status & CARRY, 0);
        assert_eq!(r.status & ZERO, 0);

        let mut run = Run::new(0x0200, &[0x38, 0xA9, 0x00, 0xE9, 0x01]);
        run.step();
        run.step();
        run.step();
        assert_eq!(run.regs().a, 0xFF);
        assert_eq!(run.regs().status & CARRY, 0);

        // $19 + $28 is $47 in decimal, and no carry.
        let mut run = Run::new(0x0200, &[0xF8, 0x18, 0xA9, 0x19, 0x69, 0x28]);
        for _ in 0..4 {
            run.step();
        }
        assert_eq!(run.regs().a, 0x47);
        assert_eq!(run.regs().status & CARRY, 0);

        // $99 + $01 is $00 with a decimal carry.
        let mut run = Run::new(0x0200, &[0xF8, 0x18, 0xA9, 0x99, 0x69, 0x01]);
        for _ in 0..4 {
            run.step();
        }
        assert_eq!(run.regs().a, 0x00);
        assert_ne!(run.regs().status & CARRY, 0);

        let mut run = Run::new(0x0200, &[0xF8, 0x38, 0xA9, 0x42, 0xE9, 0x05]);
        for _ in 0..4 {
            run.step();
        }
        assert_eq!(run.regs().a, 0x37);

        let mut run = Run::new(0x0200, &[0xF8, 0x38, 0xA9, 0x10, 0xE9, 0x01]);
        for _ in 0..4 {
            run.step();
        }
        assert_eq!(run.regs().a, 0x09);

        let mut run = Run::new(0x0200, &[0xA9, 0x10, 0xC9, 0x20]);
        run.step();
        run.step();
        assert_eq!(run.regs().status & CARRY, 0);
        assert_ne!(run.regs().status & NEGATIVE, 0);

        let mut run = Run::new(0x0200, &[0xA9, 0x20, 0xC9, 0x20]);
        run.step();
        run.step();
        assert_ne!(run.regs().status & CARRY, 0);
        assert_ne!(run.regs().status & ZERO, 0);
    }

    #[test]
    fn shift_rotate_and_memory() {
        let mut run = Run::new(0x0200, &[0xA9, 0x81, 0x0A]);
        run.step();
        run.step();
        assert_eq!(run.regs().a, 0x02);
        assert_ne!(run.regs().status & CARRY, 0);

        let mut run = Run::new(0x0200, &[0xA9, 0x01, 0x4A]);
        run.step();
        run.step();
        assert_eq!(run.regs().a, 0x00);
        assert_ne!(run.regs().status & CARRY, 0);
        assert_ne!(run.regs().status & ZERO, 0);

        let mut run = Run::new(0x0200, &[0x38, 0xA9, 0x00, 0x2A]);
        for _ in 0..3 {
            run.step();
        }
        assert_eq!(run.regs().a, 0x01);

        let mut run = Run::new(0x0200, &[0x38, 0xA9, 0x00, 0x6A]);
        for _ in 0..3 {
            run.step();
        }
        assert_eq!(run.regs().a, 0x80);

        let mut run = Run::new(0x0200, &[0xA9, 0x05, 0x85, 0x10, 0xE6, 0x10, 0xC6, 0x10]);
        for _ in 0..3 {
            run.step();
        }
        assert_eq!(run.ram.peek(0x0010), 0x06);
        run.step();
        assert_eq!(run.ram.peek(0x0010), 0x05);

        // INC and DEC on memory set Z and N from the byte they wrote. A
        // countdown loop reads Z to know it has finished, so a core that left
        // the flags alone hangs the ROM on the first count -- which is exactly
        // what this port did until the differential trace found it.
        let mut run = Run::new(0x0200, &[0xE6, 0x10]);
        run.ram.poke(0x0010, 0xFF);
        run.step();
        assert_eq!(run.ram.peek(0x0010), 0x00);
        assert_ne!(run.regs().status & ZERO, 0);
        let mut negative = Run::new(0x0200, &[0xC6, 0x10]);
        negative.ram.poke(0x0010, 0x00);
        negative.step();
        assert_eq!(negative.ram.peek(0x0010), 0xFF);
        assert_ne!(negative.regs().status & NEGATIVE, 0);
        assert_eq!(negative.regs().status & ZERO, 0);

        // BIT reads the top two bits of memory into N and V.
        let mut run = Run::new(0x0200, &[0xA9, 0x00, 0x24, 0x10]);
        run.ram.poke(0x0010, 0xC0);
        run.step();
        run.step();
        let r = run.regs();
        assert_ne!(r.status & NEGATIVE, 0);
        assert_ne!(r.status & OVERFLOW, 0);
        assert_ne!(r.status & ZERO, 0);
    }

    #[test]
    fn control_flow_and_stack() {
        // JSR/RTS: the subroutine writes a byte and returns to the caller.
        let mut ram = Ram::new();
        ram.load(0x0200, &[0x20, 0x10, 0x02, 0xA9, 0x01, 0x00]);
        ram.load(0x0210, &[0xA9, 0x42, 0x85, 0x30, 0x60]);
        let mut cpu = Cpu6502::new();
        let mut registers = cpu.registers();
        registers.pc = 0x0200;
        registers.sp = 0xFD;
        cpu.set_registers(&registers);
        cpu.step(&mut ram);
        assert_eq!(cpu.registers().pc, 0x0210);
        cpu.step(&mut ram);
        cpu.step(&mut ram);
        cpu.step(&mut ram);
        assert_eq!(ram.peek(0x0030), 0x42);
        assert_eq!(cpu.registers().pc, 0x0203);

        // A backward branch loops; a taken branch costs an extra cycle.
        let mut run = Run::new(0x0200, &[0xA2, 0x03, 0xCA, 0xD0, 0xFD]);
        assert_eq!(run.step(), 2, "LDX #3");
        assert_eq!(run.step(), 2, "DEX");
        assert_eq!(run.step(), 3, "BNE, taken, no page cross");
        assert_eq!(run.step(), 2, "DEX");
        assert_eq!(run.step(), 3, "BNE, taken");
        assert_eq!(run.step(), 2, "DEX, X is now zero");
        assert_eq!(run.regs().x, 0);
        let pc = run.regs().pc;
        assert_eq!(run.step(), 2, "BNE, not taken");
        assert_eq!(run.regs().pc, pc + 2);

        // A taken branch that crosses a page costs two extra cycles.
        let mut ram = Ram::new();
        ram.load(0x02FD, &[0xD0, 0x01]);
        ram.load(0x0300, &[0xEA, 0xEA]);
        let mut cpu = Cpu6502::new();
        let mut registers = cpu.registers();
        registers.pc = 0x02FD;
        registers.status = UNUSED; // Z clear, so the branch is taken
        cpu.set_registers(&registers);
        assert_eq!(cpu.step(&mut ram), 4);
        assert_eq!(cpu.registers().pc, 0x0300);

        // JMP (indirect) reproduces the page-boundary bug.
        let mut ram = Ram::new();
        ram.load(0x0200, &[0x6C, 0xFF, 0x30]);
        ram.poke(0x30FF, 0x00);
        ram.poke(0x3100, 0x11);
        ram.poke(0x3000, 0x40);
        let mut cpu = Cpu6502::new();
        let mut registers = cpu.registers();
        registers.pc = 0x0200;
        cpu.set_registers(&registers);
        cpu.step(&mut ram);
        assert_eq!(cpu.registers().pc, 0x4000);

        // PHA/PLA round trip.
        let mut run = Run::new(0x0200, &[0xA9, 0x7E, 0x48, 0xA9, 0x00, 0x68]);
        for _ in 0..4 {
            run.step();
        }
        assert_eq!(run.regs().a, 0x7E);
    }

    #[test]
    fn interrupts() {
        // IRQ: the handler is entered, the flags are pushed, RTI returns.
        let mut ram = Ram::new();
        ram.load(0x0200, &[0xEA, 0xEA]);
        ram.load(0x0300, &[0xA9, 0x55, 0x40]);
        ram.poke(0xFFFE, 0x00);
        ram.poke(0xFFFF, 0x03);
        let mut cpu = Cpu6502::new();
        let mut registers = cpu.registers();
        registers.pc = 0x0200;
        registers.status = UNUSED;
        cpu.set_registers(&registers);
        cpu.set_irq_line(true);
        assert_eq!(cpu.step(&mut ram), 7);
        assert_eq!(cpu.registers().pc, 0x0300);
        assert_ne!(cpu.registers().status & INTERRUPT_DISABLE, 0);
        cpu.step(&mut ram);
        assert_eq!(cpu.registers().a, 0x55);
        cpu.step(&mut ram);
        assert_eq!(cpu.registers().pc, 0x0200);
        assert_eq!(cpu.registers().status & INTERRUPT_DISABLE, 0);

        // With I set, a masked interrupt waits.
        let mut ram = Ram::new();
        ram.load(0x0200, &[0xEA]);
        ram.poke(0xFFFE, 0x00);
        ram.poke(0xFFFF, 0x03);
        let mut cpu = Cpu6502::new();
        let mut registers = cpu.registers();
        registers.pc = 0x0200;
        registers.status = UNUSED | INTERRUPT_DISABLE;
        cpu.set_registers(&registers);
        cpu.set_irq_line(true);
        assert_eq!(cpu.step(&mut ram), 2, "the NOP, not the interrupt");
        assert_eq!(cpu.registers().pc, 0x0201);

        // NMI is edge-triggered and not maskable.
        let mut ram = Ram::new();
        ram.load(0x0300, &[0xA9, 0x22, 0x40]);
        ram.poke(0xFFFA, 0x00);
        ram.poke(0xFFFB, 0x03);
        let mut cpu = Cpu6502::new();
        let mut registers = cpu.registers();
        registers.pc = 0x0200;
        registers.status = UNUSED | INTERRUPT_DISABLE;
        cpu.set_registers(&registers);
        cpu.request_nmi();
        assert_eq!(cpu.step(&mut ram), 7);
        assert_eq!(cpu.registers().pc, 0x0300);
        cpu.step(&mut ram);
        cpu.step(&mut ram);
        assert_eq!(cpu.registers().a, 0x22);
        assert_eq!(cpu.registers().pc, 0x0200);

        // BRK's vector, and the pushed status has the break bit set.
        let mut ram = Ram::new();
        ram.load(0x0200, &[0x00]);
        ram.poke(0xFFFE, 0x00);
        ram.poke(0xFFFF, 0x03);
        let mut cpu = Cpu6502::new();
        let mut registers = cpu.registers();
        registers.pc = 0x0200;
        registers.status = UNUSED;
        cpu.set_registers(&registers);
        assert_eq!(cpu.step(&mut ram), 7);
        assert_eq!(cpu.registers().pc, 0x0300);
        assert_eq!(ram.peek(0x01FD), 0x02, "high byte first");
        assert_eq!(ram.peek(0x01FC), 0x02);
        assert_ne!(ram.peek(0x01FB) & BREAK, 0);
    }

    #[test]
    fn cycle_counts() {
        let mut ram = Ram::new();
        ram.load(0x0200, &[0xA9, 0x01, 0x8D, 0x00, 0x40, 0xAD, 0x00, 0x40]);
        let mut cpu = Cpu6502::new();
        let mut registers = cpu.registers();
        registers.pc = 0x0200;
        cpu.set_registers(&registers);
        assert_eq!(cpu.step(&mut ram), 2, "LDA #");
        assert_eq!(cpu.step(&mut ram), 4, "STA absolute");
        assert_eq!(cpu.step(&mut ram), 4, "LDA absolute");
        assert_eq!(cpu.cycle_count(), 10);
        assert_eq!(cpu.instruction_count(), 3);

        // LDA abs,X costs one more when the index crosses a page.
        let mut ram = Ram::new();
        ram.load(0x0200, &[0xBD, 0x00, 0x30]);
        let mut cpu = Cpu6502::new();
        for index in [0x10u8, 0x80, 0x90] {
            let mut registers = cpu.registers();
            registers.pc = 0x0200;
            registers.x = index;
            cpu.set_registers(&registers);
            assert_eq!(cpu.step(&mut ram), 4, "no page crossed at {index:#04X}");
        }

        // A store does not earn the penalty: it always takes its five cycles.
        ram.load(0x0210, &[0x9D, 0x00, 0x30]);
        let mut registers = cpu.registers();
        registers.pc = 0x0210;
        registers.x = 0xF0;
        cpu.set_registers(&registers);
        assert_eq!(cpu.step(&mut ram), 5);

        // A frame's worth of work: 20 000 cycles is the machine's tick.
        let mut ram = Ram::new();
        ram.load(0x0200, &[0xEA, 0x4C, 0x00, 0x02]);
        let mut cpu = Cpu6502::new();
        while cpu.cycle_count() < 20_000 {
            cpu.step(&mut ram);
        }
        assert!(cpu.cycle_count() >= 20_000);
        assert_eq!(cpu.illegal_opcodes(), 0);
    }
}
