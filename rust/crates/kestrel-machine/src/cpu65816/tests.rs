// SPDX-License-Identifier: MIT
//
// The 65816 core: the two register widths, the direct page and the banks, the
// new addressing modes, the arithmetic, and the cycle counts the machine's
// real-time budget depends on. Ported from `tests/kestrel16_cpu_tests.cpp`, so
// the two trees are asked the same questions.

use super::*;

/// Sixteen megabytes of plain memory, which is the whole address space.
struct Ram {
    memory: Vec<u8>,
}

impl Ram {
    fn new() -> Self {
        Self {
            memory: vec![0; 0x1_000000],
        }
    }

    fn load(&mut self, at: u32, bytes: &[u8]) {
        let mut address = at;
        for byte in bytes {
            self.memory[(address & 0xFF_FFFF) as usize] = *byte;
            address += 1;
        }
    }

    fn poke(&mut self, address: u32, value: u8) {
        self.memory[(address & 0xFF_FFFF) as usize] = value;
    }

    fn peek(&self, address: u32) -> u8 {
        self.memory[(address & 0xFF_FFFF) as usize]
    }
}

impl Bus for Ram {
    fn read(&mut self, address: u32) -> u8 {
        self.memory[(address & 0xFF_FFFF) as usize]
    }

    fn write(&mut self, address: u32, value: u8) {
        self.memory[(address & 0xFF_FFFF) as usize] = value;
    }
}

/// A machine at a chosen starting state, with a little program in it. The
/// default is native mode with sixteen-bit registers, which is what the C++
/// tests start in.
struct Run {
    ram: Ram,
    cpu: Cpu65816,
}

const ORIGIN: u32 = 0x008000;

impl Run {
    fn new(origin: u32, program: &[u8]) -> Self {
        let mut ram = Ram::new();
        ram.load(origin, program);
        let mut cpu = Cpu65816::new();
        let mut registers = cpu.registers();
        registers.pc = origin as u16;
        registers.pbr = (origin >> 16) as u8;
        registers.s = 0x01FF;
        registers.emulation = false;
        registers.p = 0x00;
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
fn register_widths() {
    // The same opcode reads one byte or two depending on M.
    let mut run = Run::new(ORIGIN, &[0xA9, 0x34, 0x12]);
    run.step();
    assert_eq!(run.regs().c, 0x1234);
    assert_eq!(run.regs().pc, 0x8003);

    let mut run = Run::new(ORIGIN, &[0xA9, 0x34, 0x12]);
    run.set(|r| r.p = MEMORY_8);
    run.step();
    assert_eq!(run.regs().c, 0x0034);
    assert_eq!(run.regs().pc, 0x8002);

    // X is chosen by its own bit, so `LDX #$1234` reads two bytes even when the
    // accumulator is eight bits wide.
    let mut run = Run::new(ORIGIN, &[0xA2, 0x34, 0x12]);
    run.set(|r| r.p = MEMORY_8);
    run.step();
    assert_eq!(run.regs().x, 0x1234);

    // Switching the index to eight bits clears the high bytes.
    let mut run = Run::new(ORIGIN, &[0xA2, 0x34, 0x12, 0xE2, 0x10]);
    run.step();
    run.step();
    assert_eq!(run.regs().x, 0x0034);
    assert_eq!(run.regs().p & INDEX_8, INDEX_8);
}

#[test]
fn jumps_stay_in_their_bank() {
    // JMP is a jump inside the program bank: the address is sixteen bits and
    // the bank does not move, whatever the data bank holds.
    let mut run = Run::new(0x018000, &[0x4C, 0x34, 0x12]);
    run.set(|r| r.dbr = 0x07);
    run.step();
    assert_eq!(run.regs().pc, 0x1234);
    assert_eq!(run.regs().pbr, 0x01);

    // JML is the long form, and the bank is part of the address.
    let mut run = Run::new(ORIGIN, &[0x5C, 0x34, 0x12, 0x03]);
    run.step();
    assert_eq!(run.regs().pc, 0x1234);
    assert_eq!(run.regs().pbr, 0x03);

    // JSR follows the same rule as its jump.
    let mut run = Run::new(0x018000, &[0x20, 0x34, 0x12]);
    run.set(|r| r.dbr = 0x07);
    run.step();
    assert_eq!(run.regs().pc, 0x1234);
    assert_eq!(run.regs().pbr, 0x01);
}

#[test]
fn emulation_mode_is_the_6502() {
    // Emulation mode is the 6502: the registers are eight bits, the stack is in
    // page one, and the direct page is zero however it was set.
    let mut run = Run::new(ORIGIN, &[0x38, 0xFB]);
    run.set(|r| {
        r.emulation = true;
        r.p = 0x34;
        r.s = 0x01FF;
        r.c = 0x1234;
        r.d = 0x4321;
    });
    run.step();
    run.step();
    assert!(run.regs().emulation);
    assert_eq!(run.regs().p & MEMORY_8, MEMORY_8);
    assert_eq!(run.regs().d, 0x0000);

    // Leaving emulation is one instruction, and the carry records where the
    // machine was: it ends up set, because emulation was on.
    let mut leave = Run::new(ORIGIN, &[0x18, 0xFB]);
    leave.set(|r| {
        r.emulation = true;
        r.p = 0x34;
    });
    leave.step();
    leave.step();
    assert!(!leave.regs().emulation);
    assert_eq!(leave.regs().p & CARRY, CARRY);
}

#[test]
fn banks() {
    // An absolute address points into the data bank, a long one names its own.
    let mut run = Run::new(ORIGIN, &[0x8D, 0x00, 0x20, 0x8F, 0x00, 0x21, 0x7E]);
    run.set(|r| {
        r.c = 0xABCD;
        r.dbr = 0x05;
    });
    run.step();
    run.step();
    assert_eq!(run.ram.peek(0x052000), 0xCD);
    assert_eq!(run.ram.peek(0x052001), 0xAB);
    assert_eq!(run.ram.peek(0x7E2100), 0xCD);
    assert_eq!(run.ram.peek(0x7E2101), 0xAB);
}

#[test]
fn direct_page() {
    // A direct page that is not page zero is reachable, costs a cycle, and
    // indexes into itself.
    let mut run = Run::new(ORIGIN, &[0xB5, 0x10]);
    run.ram.poke(0x0214, 0x34);
    run.ram.poke(0x0215, 0x12);
    run.set(|r| {
        r.d = 0x0200;
        r.x = 0x04;
        r.p = MEMORY_8;
    });
    let cycles = run.step();
    assert_eq!(run.regs().c, 0x0034);
    // Four for the instruction, plus one because the index is sixteen bits
    // wide: a wide index always pays for the address it might need.
    assert_eq!(cycles, 5);

    // A direct page that starts mid-page costs a cycle on every access.
    let mut straddle = Run::new(ORIGIN, &[0xB5, 0x10]);
    straddle.ram.poke(0x0315, 0x34);
    straddle.set(|r| {
        r.d = 0x0201;
        r.x = 0x04;
        r.p = MEMORY_8;
    });
    assert_eq!(straddle.step(), 6, "five for the wide index, one for the page");
}

#[test]
fn new_address_modes() {
    // [dp] reads a three-byte pointer, so a store through it reaches a bank.
    let mut run = Run::new(ORIGIN, &[0x87, 0x10]);
    run.ram.poke(0x0010, 0x00);
    run.ram.poke(0x0011, 0x21);
    run.ram.poke(0x0012, 0x7E);
    run.set(|r| {
        r.c = 0x0055;
        r.p = MEMORY_8;
    });
    run.step();
    assert_eq!(run.ram.peek(0x7E2100), 0x55);

    // A stack-relative operand addresses the stack wherever it is.
    let mut run = Run::new(ORIGIN, &[0xA3, 0x02]);
    run.ram.poke(0x0201, 0x77);
    run.step();
    assert_eq!(run.regs().c, 0x0077);

    // (dp,S),Y goes through a pointer on the stack.
    let mut run = Run::new(ORIGIN, &[0xB3, 0x02]);
    run.ram.poke(0x0201, 0x00);
    run.ram.poke(0x0202, 0x20);
    run.ram.poke(0x002003, 0x9A);
    run.set(|r| {
        r.y = 0x03;
        r.p = MEMORY_8;
    });
    run.step();
    assert_eq!(run.regs().c, 0x009A);

    // A long,X operand carries the index into the bank.
    let mut run = Run::new(ORIGIN, &[0xBF, 0xFF, 0xFF, 0x7E]);
    run.ram.poke(0x7F0001, 0x42);
    run.set(|r| {
        r.x = 0x0002;
        r.p = MEMORY_8;
    });
    run.step();
    assert_eq!(run.regs().c, 0x0042);
}

#[test]
fn arithmetic() {
    // Sixteen-bit addition and its carry.
    let mut run = Run::new(ORIGIN, &[0x69, 0xFF, 0xFF]);
    run.set(|r| r.c = 0x0001);
    run.step();
    assert_eq!(run.regs().c, 0x0000);
    assert_eq!(run.regs().p & CARRY, CARRY);
    assert_eq!(run.regs().p & ZERO, ZERO);

    // Decimal mode adds a digit at a time, sixteen bits wide.
    let mut run = Run::new(ORIGIN, &[0x69, 0x55, 0x55]);
    run.set(|r| {
        r.c = 0x5555;
        r.p = DECIMAL;
    });
    run.step();
    assert_eq!(run.regs().c, 0x1110);
    assert_eq!(run.regs().p & CARRY, CARRY);

    // Decimal subtraction borrows a digit at a time.
    let mut run = Run::new(ORIGIN, &[0xE9, 0x01, 0x00]);
    run.set(|r| {
        r.c = 0x0042;
        r.p = DECIMAL | CARRY;
    });
    run.step();
    assert_eq!(run.regs().c, 0x0041);

    // Eight-bit decimal, the mode the old code paths use.
    let mut run = Run::new(ORIGIN, &[0x69, 0x01]);
    run.set(|r| {
        r.c = 0x0009;
        r.p = DECIMAL | MEMORY_8;
    });
    run.step();
    assert_eq!(run.regs().c & 0xFF, 0x10);

    // The overflow flag is about signed arithmetic.
    let mut run = Run::new(ORIGIN, &[0x69, 0x01]);
    run.set(|r| {
        r.c = 0x007F;
        r.p = MEMORY_8;
    });
    run.step();
    assert_eq!(run.regs().p & OVERFLOW, OVERFLOW);
}

/// A memory increment is as wide as the accumulator. It used to be forced to a
/// byte here, on a comment that claimed the processor had no sixteen-bit `INC`;
/// it has one, and the flags come from the whole word.
#[test]
fn memory_increments_follow_the_accumulator_width() {
    // Sixteen-bit: $7EFF + 1 carries into the byte above it.
    let mut run = Run::new(ORIGIN, &[0xEE, 0x00, 0x30]); // INC $3000
    run.ram.poke(0x3000, 0xFF);
    run.ram.poke(0x3001, 0x7E);
    run.step();
    assert_eq!(run.ram.peek(0x3000), 0x00);
    assert_eq!(run.ram.peek(0x3001), 0x7F, "the carry went into the high byte");

    let mut dec = Run::new(ORIGIN, &[0xCE, 0x00, 0x30]); // DEC $3000
    dec.ram.poke(0x3000, 0x00);
    dec.ram.poke(0x3001, 0x01);
    dec.step();
    assert_eq!(dec.ram.peek(0x3000), 0xFF);
    assert_eq!(dec.ram.peek(0x3001), 0x00, "and the borrow came out of it");

    // Eight-bit: one byte, and the byte above it untouched.
    let mut byte = Run::new(ORIGIN, &[0xE6, 0x30]); // INC $30
    byte.ram.poke(0x0030, 0xFF);
    byte.ram.poke(0x0031, 0x7E);
    byte.set(|r| r.p = MEMORY_8);
    byte.step();
    assert_eq!(byte.ram.peek(0x0030), 0x00);
    assert_eq!(byte.ram.peek(0x0031), 0x7E, "nothing above the byte was touched");
    assert_eq!(byte.regs().p & ZERO, ZERO);

    // The accumulator forms follow M too, as they always did.
    let mut wide = Run::new(ORIGIN, &[0x1A]); // INC A
    wide.set(|r| r.c = 0xFFFF);
    wide.step();
    assert_eq!(wide.regs().c, 0x0000);
}

#[test]
fn stack_and_transfers() {
    // A sixteen-bit push puts the high byte away first.
    let mut run = Run::new(ORIGIN, &[0xF4, 0x34, 0x12, 0x68]);
    run.step();
    run.step();
    assert_eq!(run.regs().c, 0x1234);
    assert_eq!(run.regs().s, 0x01FF);

    // XBA swaps the halves of the accumulator.
    let mut run = Run::new(ORIGIN, &[0xEB]);
    run.set(|r| r.c = 0x1234);
    run.step();
    assert_eq!(run.regs().c, 0x3412);

    // A long call saves the program bank, and returning restores it.
    let mut run = Run::new(ORIGIN, &[0x22, 0x00, 0x90, 0x02]);
    run.step();
    assert_eq!(run.regs().pbr, 0x02);
    assert_eq!(run.regs().pc, 0x9000);
    run.ram.poke(0x029000, 0x6B); // RTL
    run.step();
    assert_eq!(run.regs().pbr, 0x00);
    assert_eq!(run.regs().pc, 0x8004);
}

#[test]
fn block_move() {
    // MVN copies C+1 bytes, walking both banks upward.
    let mut run = Run::new(ORIGIN, &[0x54, 0x7E, 0x7F]);
    run.ram.poke(0x7F1000, 0x11);
    run.ram.poke(0x7F1001, 0x22);
    run.ram.poke(0x7F1002, 0x33);
    run.set(|r| {
        r.c = 0x0002; // three bytes
        r.x = 0x1000;
        r.y = 0x2000;
    });
    run.step();
    assert_eq!(run.ram.peek(0x7E2000), 0x11);
    assert_eq!(run.ram.peek(0x7E2001), 0x22);
    assert_eq!(run.ram.peek(0x7E2002), 0x33);
    assert_eq!(run.regs().x, 0x1003);
    assert_eq!(run.regs().y, 0x2003);
    assert_eq!(run.regs().dbr, 0x7E);
}

#[test]
fn cycle_counts() {
    // The counts a frame budget is written against.
    let mut run = Run::new(ORIGIN, &[0xEA]);
    assert_eq!(run.step(), 2, "NOP");

    let mut run = Run::new(ORIGIN, &[0xAD, 0x34, 0x12]);
    assert_eq!(run.step(), 4, "LDA absolute");

    // A page crossing costs a cycle for a read with an eight-bit index.
    let mut run = Run::new(ORIGIN, &[0xBD, 0xFF, 0x20]);
    run.set(|r| {
        r.x = 0x02;
        r.p = MEMORY_8 | INDEX_8;
    });
    assert_eq!(run.step(), 5);

    // With a sixteen-bit index the extra cycle is always there.
    let mut wide = Run::new(ORIGIN, &[0xBD, 0xFF, 0x20]);
    wide.set(|r| {
        r.x = 0x0002;
        r.p = MEMORY_8;
    });
    assert_eq!(wide.step(), 5);

    // A taken branch costs three, an untaken one two.
    let mut taken = Run::new(ORIGIN, &[0xD0, 0x02]);
    taken.set(|r| r.p = 0);
    assert_eq!(taken.step(), 3);

    let mut untaken = Run::new(ORIGIN, &[0xD0, 0x02]);
    untaken.set(|r| r.p = ZERO);
    assert_eq!(untaken.step(), 2);
}

#[test]
fn irq() {
    // An interrupt saves the program bank in native mode and vectors through
    // the native table.
    let mut run = Run::new(ORIGIN, &[0xEA]);
    run.ram.poke(0xFFEE, 0x00);
    run.ram.poke(0xFFEF, 0x90);
    run.cpu.set_irq_line(true);
    assert_eq!(run.step(), 8);
    assert_eq!(run.regs().pc, 0x9000);
    assert_eq!(run.regs().p & IRQ_DISABLE, IRQ_DISABLE);
}

/// `LDX`, `LDY`, `STX`, `STY` and the memory forms of `CPX`/`CPY` are as wide as
/// the index registers, not as wide as the accumulator. Reading them M-wide cost
/// the KERNAL the high byte of every sector number above 255, so a disk read went
/// to sector zero.
#[test]
fn index_width_operands_follow_the_index_flag() {
    // Sixteen-bit index, eight-bit accumulator: STX writes both bytes.
    let mut run = Run::new(ORIGIN, &[0x8E, 0x00, 0x30]); // STX $3000
    run.set(|r| {
        r.x = 0x1234;
        r.p = MEMORY_8;
    });
    run.step();
    assert_eq!(run.ram.peek(0x3000), 0x34);
    assert_eq!(run.ram.peek(0x3001), 0x12, "the high byte went too");

    // And LDX reads both.
    let mut run = Run::new(ORIGIN, &[0xAE, 0x00, 0x30]); // LDX $3000
    run.ram.poke(0x3000, 0x78);
    run.ram.poke(0x3001, 0x56);
    run.set(|r| r.p = MEMORY_8);
    run.step();
    assert_eq!(run.regs().x, 0x5678);

    // An eight-bit index sees only the low byte, and STY leaves the byte above
    // it alone.
    let mut run = Run::new(ORIGIN, &[0x84, 0x30, 0x8C, 0x00, 0x30]); // STY $30 ; STY $3000
    run.ram.poke(0x3001, 0xAA);
    run.ram.poke(0x0031, 0xBB);
    run.set(|r| {
        r.y = 0x1234;
        r.p = MEMORY_8 | INDEX_8;
    });
    run.step();
    run.step();
    assert_eq!(run.ram.peek(0x3000), 0x34);
    assert_eq!(run.ram.peek(0x3001), 0xAA, "nothing above the byte was written");
}

#[test]
fn the_table_is_the_cpp_table() {
    // Every opcode the C++ map names, at the cycle count it names.
    let mut adc = 0;
    for entry in TABLE.iter() {
        if entry.op == Op::Adc {
            adc += 1;
        }
    }
    assert_eq!(adc, 15, "the fifteen ADC forms, one per mode");
    assert_eq!(TABLE[0x00].op, Op::Brk);
    assert_eq!(TABLE[0x00].mode, Mode::Sig);
    assert_eq!(TABLE[0x00].cycles, 7);
    assert_eq!(TABLE[0x54].op, Op::Mvn);
    assert_eq!(TABLE[0x5C].op, Op::Jml);
    assert_eq!(TABLE[0x5C].cycles, 5);
    assert_eq!(TABLE[0x6B].op, Op::Rtl);
    assert_eq!(TABLE[0xFB].op, Op::Xce);
    assert_eq!(TABLE[0xFB].cycles, 2);
}
