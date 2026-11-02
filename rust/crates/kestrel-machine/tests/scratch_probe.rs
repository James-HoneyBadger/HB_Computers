// Temporary probe: not part of the suite.
use std::path::Path;

use kestrel_machine::machine::{Cpu, Machine};
use kestrel_machine::profile::Model;

fn type_line(m: &mut Machine, text: &str) {
    for byte in text.bytes().chain(std::iter::once(13u8)) {
        while m.bus.keyboard.len() >= 8 {
            m.run_frames(1);
        }
        m.type_key(byte);
        m.run_frames(2);
    }
    while !m.bus.keyboard.is_empty() {
        m.run_frames(1);
    }
}

fn word(m: &mut Machine, low: u16) -> u16 {
    u16::from(m.bus.read(u32::from(low))) | (u16::from(m.bus.read(u32::from(low) + 1)) << 8)
}

#[test]
fn probe() {
    let path = Path::new("../../../.scratch/dos/probe.dsk");
    std::fs::write(path, vec![0u8; 4096 * 512]).expect("blank volume");
    let mut m = Machine::new(Model::Kestrel16);
    m.mount_assembled_roms().expect("roms");
    m.reset();
    m.mount_disk_image(path).expect("disk");
    m.run_frames(25);
    type_line(&mut m, "FORMAT");
    m.run_frames(20);
    type_line(&mut m, "10 PRINT \"HELLO\"");
    m.run_frames(20);
    // Type SAVE with the Return delivered last, then watch.
    for byte in b"SAVE \"TEST\"".iter().copied() {
        while m.bus.keyboard.len() >= 8 {
            m.run_frames(1);
        }
        m.type_key(byte);
        m.run_frames(2);
    }
    while !m.bus.keyboard.is_empty() {
        m.run_frames(1);
    }
    m.type_key(13);
    let mut trail: Vec<(u32, u16, u16)> = Vec::new();
    let mut before = 0u64;
    {
        let Cpu::Mos65816(ref cpu) = m.cpu else { panic!() };
        before = cpu.illegal_opcodes();
    }
    for _ in 0..40_000_000 {
        m.run_cycles(1);
        let Cpu::Mos65816(ref cpu) = m.cpu else { panic!() };
        let r = cpu.registers();
        let pc = (u32::from(r.pbr) << 16) | u32::from(r.pc);
        trail.push((pc, r.s, r.c));
        if trail.len() > 90 {
            trail.remove(0);
        }
        if cpu.illegal_opcodes() > before {
            println!("illegal opcode {:02X} at the trail:", cpu.last_illegal_opcode());
            for (pc, s, c) in trail.iter().skip(trail.len().saturating_sub(28)) {
                println!("   pc {pc:06X} s {s:04X} a {c:04X}");
            }
            break;
        }
        if r.p & 0x04 != 0 && pc == 0x010000 {
            println!("back at BASIC ENTRY; trail tail:");
            for (pc, s, c) in trail.iter().skip(trail.len().saturating_sub(28)) {
                println!("   pc {pc:06X} s {s:04X} a {c:04X}");
            }
            break;
        }
    }
    println!(
        "ZP: scan={:04X} count={:04X} left={:04X} first={:04X} sec={:04X} len={:04X} idx={:04X}",
        word(&mut m, 0x7D),
        word(&mut m, 0x6B),
        word(&mut m, 0x71),
        word(&mut m, 0x69),
        word(&mut m, 0x79),
        word(&mut m, 0x6F),
        word(&mut m, 0x6D),
    );
    println!("disk map bytes: {:02X?}", &m.bus.disk.image()[32..40]);
    println!("disk sig: {:?}", &m.bus.disk.image()[0..8]);
    println!("buf at 020000: {:02X?}", &(0..16).map(|i| m.bus.read(0x020000 + i)).collect::<Vec<_>>());
    println!("buf at 020020: {:02X?}", &(0..16).map(|i| m.bus.read(0x020020 + i)).collect::<Vec<_>>());
    println!("ptr={:02X}:{:04X} flag={:02X}", m.bus.read(0x86), word(&mut m, 0x84), m.bus.read(0x83));
    println!("---- screen ----");
    for row in 0..12 {
        println!("  |{}|", m.screen_line(row));
    }
}
