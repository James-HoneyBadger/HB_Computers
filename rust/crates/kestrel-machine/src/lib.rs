// SPDX-License-Identifier: MIT
//
// kestrel-machine -- the port of `kestrel_machine`, crate by crate.
//
// What is here: the profile table and the four devices, with the tests that
// prove them. What is next, in this order: the bus decode and the I/O page, the
// 6502, the 65816, then the machine that binds them. The C++ tree stays until
// this one can do everything it does.
//
// The port follows the original file by file, so a reader can put the two side by
// side: `profile.rs` is `makeProfile`, `devices.rs` is `devices.cpp`.

pub mod bus;
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
