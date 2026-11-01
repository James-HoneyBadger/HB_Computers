// A frame-by-frame hash of the machine, for finding where the Rust port and the
// C++ tree first disagree.
//
//   cargo run --example differential > rust.txt
//
// The counterpart is thirty lines of C++ against `libkestrel_machine.a` -- the
// same loop, printing the same line, with `machine.ramAt(address)` and
// `machine.video().cell(i)` standing in for the fields -- and `diff` names the
// first frame that differs. That is how the 6502's missing INC/DEC flags were
// found: everything matched until one frame, one instruction, and one bit of the
// status register.
use kestrel_machine::machine::{Cpu, Machine};
use kestrel_machine::profile::Model;

fn main() {
    let mut machine = Machine::new(Model::Kestrel);
    let images = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../../build/generated");
    machine.mount_shipped_roms(&images).unwrap();
    machine.reset();
    machine.run_frames(25);
    for byte in "PRINT \"A\"".bytes() {
        machine.type_key(byte);
    }
    machine.type_key(0x0D);
    for frame in 0..60 {
        machine.run_frames(1);
        let mut ram: u32 = 2166136261;
        for address in 0..0x8000usize {
            ram = (ram ^ u32::from(machine.bus.ram[address])).wrapping_mul(16777619);
        }
        let mut screen: u32 = 2166136261;
        for index in 0..machine.bus.video.cells() {
            screen = (screen ^ u32::from(machine.bus.video.cell(index))).wrapping_mul(16777619);
        }
        let (pc, a, x, y, sp, p) = match machine.cpu {
            Cpu::Mos6502(ref cpu) => {
                let r = cpu.registers();
                (r.pc, r.a, r.x, r.y, r.sp, r.status)
            }
            _ => panic!(),
        };
        println!(
            "frame {frame} pc={pc:04X} a={a:02X} x={x:02X} y={y:02X} sp={sp:02X} p={p:02X} ram={ram:08X} scr={screen:08X}"
        );
    }
}
