// SPDX-License-Identifier: MIT
//
// kestrel-machine -- the port of `kestrel_machine`, crate by crate.
//
// What is here: the profile table, the four devices, the bus decode with its
// I/O page, and the 1982 machine's processor -- each with the tests that prove
// it. What is next, in this order: the 65816, the machine that binds a processor
// to a bus, the ROMs, the command line, and the window. The C++ tree stays until
// this one can do everything it does.
//
// The port follows the original file by file, so a reader can put the two side by
// side: `profile.rs` is `makeProfile`, `devices.rs` is `devices.cpp`.

pub mod bus;
pub mod cpu6502;
pub mod devices;
pub mod profile;

pub use bus::{Bus, Register};
pub use devices::{Devices, Disk, Keyboard, SpecialKey, Tape, TapeMode, Video};
pub use profile::{profile_of, Model, Profile, KESTREL, KESTREL16};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_profiles_are_the_cpp_profiles() {
        let kestrel = profile_of(Model::Kestrel);
        assert_eq!(kestrel.clock_hz, 1_000_000);
        assert_eq!(kestrel.cycles_per_frame, 20_000);
        assert_eq!(kestrel.ram_bytes, 0x8000);
        assert_eq!(kestrel.screen_cells(), 1000);
        assert_eq!(kestrel.attribute_base, 0x8400);
        assert_eq!(kestrel.disk_sectors * kestrel.disk_sector_size, 131_072);
        assert_eq!(kestrel.keyboard_capacity, 16);

        let kestrel16 = profile_of(Model::Kestrel16);
        assert_eq!(kestrel16.clock_hz, 8_000_000);
        assert_eq!(kestrel16.cycles_per_frame, 160_000);
        assert_eq!(kestrel16.ram_bytes, 0x10_0000);
        assert_eq!(kestrel16.screen_cells(), 4000);
        assert_eq!(kestrel16.attribute_base, 0x8FA0);
        assert_eq!(kestrel16.disk_sectors * kestrel16.disk_sector_size, 2_097_152);
        assert_eq!(kestrel16.basic_base, 0x010000);
        assert_eq!(kestrel16.keyboard_capacity, 32);
    }

    #[test]
    fn the_bus_answers_in_the_documented_order() {
        use bus::Bus;
        let mut kestrel = Bus::new(&KESTREL);
        // RAM first: $0000 is memory.
        kestrel.write(0x0010, 0x42);
        assert_eq!(kestrel.read(0x0010), 0x42);
        // The tube at $8000, its attributes at $8400.
        kestrel.write(0x8000, b'A');
        assert_eq!(kestrel.read(0x8000), b'A');
        kestrel.write(0x8400, 0x1E);
        assert_eq!(kestrel.read(0x8400), 0x1E);
        // The padding between them reads as a space.
        assert_eq!(kestrel.read(0x83E8), b' ');
        // The I/O page: ink, paper, the sound latch and its count.
        kestrel.write(0x8805, 0x03);
        assert_eq!(kestrel.read(0x8805), 0x03);
        kestrel.write(0x8808, 60);
        assert_eq!(kestrel.notes_played, 1);
        // A key goes in and comes out of the same register.
        kestrel.keyboard.push(b'Z');
        assert_eq!(kestrel.read(0x8801), 0xFF);
        assert_eq!(kestrel.read(0x8800), b'Z');
        assert_eq!(kestrel.read(0x8801), 0x00);
    }

    #[test]
    fn the_1992_bus_puts_the_kernal_over_its_ram() {
        use bus::Bus;
        let mut kestrel16 = Bus::new(&KESTREL16);
        kestrel16.kernal[0] = 0x99;
        assert_eq!(kestrel16.read(0x00E000), 0x99, "the KERNAL wins in bank zero");
        kestrel16.write(0x00E000, 0x11);
        assert_eq!(kestrel16.read(0x00E000), 0x99, "and writes to ROM are lost");
        // Bank one is the language.
        kestrel16.basic[0] = 0x5A;
        assert_eq!(kestrel16.read(0x010000), 0x5A);
    }

    #[test]
    fn the_1982_disk_moves_a_sector_through_its_pointer() {
        use bus::Bus;
        let mut kestrel = Bus::new(&KESTREL);

        // A sector is written, then read back into RAM through the buffer pointer.
        let mut sector = vec![0u8; 256];
        for (i, slot) in sector.iter_mut().enumerate() {
            *slot = i as u8;
        }
        assert!(kestrel.disk.write_sector(5, &sector));
        kestrel.write(0x8810, 0x00);  // buffer pointer, low
        kestrel.write(0x8811, 0x10);  // and high: $1000
        kestrel.write(0x880C, 5);     // sector
        kestrel.write(0x880D, 0);
        kestrel.write(0x880E, 1);     // read
        assert_eq!(kestrel.read(0x880F), 0x00, "no news is good news");
        assert_eq!(kestrel.read(0x1000), 0);
        assert_eq!(kestrel.read(0x10FF), 0xFF, "the whole sector moved");

        // And back out again.
        for address in 0x1000..0x1100u32 {
            kestrel.write(address, 0x5A);
        }
        kestrel.write(0x880C, 6);
        kestrel.write(0x880E, 2);     // write
        let mut back = vec![0u8; 256];
        assert!(kestrel.disk.read_sector(6, &mut back));
        assert_eq!(back[0], 0x5A);
        assert_eq!(back[255], 0x5A);

        // The query register answers with the volume's size.
        kestrel.write(0x880E, 3);
        assert_eq!(kestrel.read(0x8812), 512u16 as u8);
        assert_eq!(kestrel.read(0x8813), 2, "five hundred and twelve sectors");

        // Past the end of the volume, and an unknown command.
        kestrel.write(0x880C, 0xFF);
        kestrel.write(0x880D, 0xFF);
        kestrel.write(0x880E, 1);
        assert_eq!(kestrel.read(0x880F), 0x81, "no such sector");
        kestrel.write(0x880E, 9);
        assert_eq!(kestrel.read(0x880F), 0x82, "no such command");

        // With the volume out, the controller says so and does nothing.
        kestrel.disk.eject();
        kestrel.write(0x880E, 1);
        assert_eq!(kestrel.read(0x880F), 0x80, "no disk");
    }

    #[test]
    fn the_1992_disk_moves_a_run_of_sectors() {
        use bus::Bus;
        let mut kestrel16 = Bus::new(&KESTREL16);
        assert_eq!(kestrel16.read(0xA013), 0, "the count starts at nought");
        assert_eq!(kestrel16.read(0xA016), 0, "and no key has been read");

        // Two sectors out of the same volume, one after the other.
        for sector_number in 5..7u32 {
            let mut sector = vec![sector_number as u8; 512];
            sector[0] = 0xA0 + sector_number as u8;
            assert!(kestrel16.disk.write_sector(sector_number, &sector));
        }
        kestrel16.write(0xA010, 0x00);  // $001000
        kestrel16.write(0xA011, 0x10);
        kestrel16.write(0xA012, 0x00);
        kestrel16.write(0xA00C, 5);
        kestrel16.write(0xA00D, 0);
        kestrel16.write(0xA013, 2);     // two sectors in this run
        kestrel16.write(0xA00E, 1);     // read
        assert_eq!(kestrel16.read(0xA00F), 0x84, "present and done");
        assert_eq!(kestrel16.read(0x001000), 0xA5);
        assert_eq!(kestrel16.read(0x001200), 0xA6, "the second sector followed it");
        assert_eq!(kestrel16.read(0x001200 + 511), 6);

        // The count is a setting on this machine, and nought means one.
        kestrel16.write(0xA013, 0);
        for address in 0x002000..0x002200u32 {
            kestrel16.write(address, 0x3C);
        }
        kestrel16.write(0xA010, 0x00);
        kestrel16.write(0xA011, 0x20);
        kestrel16.write(0xA00C, 7);
        kestrel16.write(0xA00E, 2);     // write
        let mut back = vec![0u8; 512];
        assert!(kestrel16.disk.read_sector(7, &mut back));
        assert_eq!(back[0], 0x3C);
        assert_eq!(back[511], 0x3C);
        assert_eq!(kestrel16.read(0xA00F), 0x84);

        // A sector the volume has not got sets the error bit.
        kestrel16.write(0xA00C, 0x00);
        kestrel16.write(0xA00D, 0x10);  // sector 4096, one past the last
        kestrel16.write(0xA00E, 1);
        assert_eq!(kestrel16.read(0xA00F), 0x06, "present, error, not done");
    }

    #[test]
    fn the_1992_machine_answers_a_flag_with_its_sign() {
        use bus::Bus;
        let mut kestrel = Bus::new(&KESTREL);
        kestrel.keyboard.push(b'A');
        assert_eq!(kestrel.read(0x8801), 0xFF, "the 1982 machine says all ones");
        assert_eq!(kestrel.read(0x8800), b'A');
        assert_eq!(kestrel.read(0x8815), b'A', "and remembers what it read");
        assert_eq!(kestrel.read(0x8801), 0x00);

        let mut kestrel16 = Bus::new(&KESTREL16);
        kestrel16.keyboard.push(b'A');
        assert_eq!(kestrel16.read(0xA001), 0x80, "the 65816 says its sign bit");
        assert_eq!(kestrel16.read(0xA000), b'A');
        assert_eq!(kestrel16.read(0xA016), b'A');
        assert_eq!(kestrel16.read(0xA001), 0x00);

        // The cursor is kept inside the tube on both, which is 80 columns on one
        // and 40 on the other.
        kestrel.write(0x8803, 200);
        assert_eq!(kestrel.read(0x8803), 39);
        kestrel16.write(0xA003, 200);
        assert_eq!(kestrel16.read(0xA003), 79);
        kestrel16.write(0xA004, 200);
        assert_eq!(kestrel16.read(0xA004), 49);
    }

    #[test]
    fn a_keyboard_refuses_a_sixteenth_key() {
        let mut keyboard = Keyboard::new(16);
        for key in 0..16u8 {
            assert!(keyboard.push(key + 1));
        }
        assert!(!keyboard.push(99), "the seventeenth key is refused");
        assert_eq!(keyboard.len(), 16);
        assert_eq!(keyboard.read(), 1);
        assert!(keyboard.push(99), "with room, the key gets in");
    }

    #[test]
    fn the_tube_holds_cells_and_attributes_apart() {
        let mut video = Video::new(40, 25);
        video.power_on(0x07, 0x00);
        assert_eq!(video.cells(), 1000);
        assert_eq!(video.attribute(0), 0x70);
        video.set_cell(41, b'A');
        video.set_attribute(41, 0x1E);
        assert_eq!(video.cell(41), b'A');
        assert_eq!(video.attribute(41), 0x1E);
        assert_eq!(video.line(1), " A", "column one, trailing spaces gone");
        assert_eq!(video.line(0), "");
        assert_eq!(video.line(25), "", "past the last row is empty");
    }

    #[test]
    fn a_disk_is_sectors_and_a_name() {
        let mut disk = Disk::new(512, 256);
        assert_eq!(disk.volume_name(), "KESWARP");
        assert!(disk.present());

        let mut sector = vec![0u8; 256];
        sector[0] = 0xAB;
        assert!(disk.write_sector(3, &sector));
        let mut read = vec![0u8; 256];
        assert!(disk.read_sector(3, &mut read));
        assert_eq!(read[0], 0xAB);

        assert!(!disk.read_sector(512, &mut read), "past the last sector");
        assert!(!disk.load(&[0u8; 100]), "a volume of the wrong size is refused");

        disk.eject();
        assert!(!disk.present());
        disk.insert();
        assert!(disk.present());
    }

    #[test]
    fn the_tape_says_what_it_is_doing() {
        let mut tape = Tape::new();
        assert_eq!(tape.status(), 0);
        tape.insert();
        assert_eq!(tape.status() & 0x80, 0x80, "present");

        tape.load(&[1, 2, 3]);
        tape.play();
        assert_eq!(tape.status() & 0x10, 0x10, "more to read");
        assert_eq!(tape.read_byte(), 1);
        assert_eq!(tape.read_byte(), 2);
        assert_eq!(tape.read_byte(), 3);
        assert_eq!(tape.read_byte(), 0, "nothing left reads as zero");
        assert_eq!(tape.status() & 0x10, 0, "and it says so");

        tape.record();
        tape.rewind();
        tape.write_byte(9);
        assert_eq!(tape.status() & 0x20, 0x20, "recording");
    }
}
