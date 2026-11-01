// SPDX-License-Identifier: MIT
//
// The two machines' numbers, in one table, exactly as `makeProfile` in
// `src/kestrel/machine.cpp` writes them down. Nothing else in the port knows
// which machine it is running: the decode, the devices and (when they arrive) the
// processors all ask the profile.

/// Which machine this is.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Model {
    /// The 1982 machine: a 6502, 32 KiB, a 40 x 25 tube.
    Kestrel,
    /// The 1992 machine: a 65816, a megabyte of banks, an 80 x 50 tube.
    Kestrel16,
}

/// Everything that differs between the two.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Profile {
    pub model: Model,
    pub name: &'static str,
    pub clock_hz: u32,
    pub frames_per_second: u32,
    pub cycles_per_frame: u32,
    /// Bytes of RAM the machine answers for, from address zero up.
    pub ram_bytes: u32,
    pub screen_base: u32,
    pub columns: u16,
    pub rows: u16,
    pub attribute_base: u32,
    pub io_base: u32,
    pub io_size: u32,
    pub cartridge_base: u32,
    pub cartridge_size: u32,
    pub kernal_base: u32,
    pub kernal_size: u32,
    pub basic_base: u32,
    pub basic_size: u32,
    pub disk_sectors: u32,
    pub disk_sector_size: u32,
    pub keyboard_capacity: usize,
}

impl Profile {
    pub fn screen_cells(&self) -> u32 {
        u32::from(self.columns) * u32::from(self.rows)
    }
}

pub const KESTREL: Profile = Profile {
    model: Model::Kestrel,
    name: "Kestrel",
    clock_hz: 1_000_000,
    frames_per_second: 50,
    cycles_per_frame: 20_000,
    ram_bytes: 0x8000,
    screen_base: 0x8000,
    columns: 40,
    rows: 25,
    attribute_base: 0x8400,
    io_base: 0x8800,
    io_size: 0x0100,
    cartridge_base: 0x9000,
    cartridge_size: 0x1000,
    kernal_base: 0xE000,
    kernal_size: 8192,
    basic_base: 0xA000,
    basic_size: 16384,
    disk_sectors: 512,
    disk_sector_size: 256,
    keyboard_capacity: 16,
};

pub const KESTREL16: Profile = Profile {
    model: Model::Kestrel16,
    name: "Kestrel 16",
    clock_hz: 8_000_000,
    frames_per_second: 50,
    cycles_per_frame: 160_000,
    ram_bytes: 0x100000,
    screen_base: 0x8000,
    columns: 80,
    rows: 50,
    attribute_base: 0x8FA0,
    io_base: 0xA000,
    io_size: 0x0100,
    cartridge_base: 0xC000,
    cartridge_size: 0x20000,
    kernal_base: 0xE000,
    kernal_size: 8192,
    basic_base: 0x010000,
    basic_size: 65536,
    disk_sectors: 4096,
    disk_sector_size: 512,
    keyboard_capacity: 32,
};

pub fn profile_of(model: Model) -> &'static Profile {
    match model {
        Model::Kestrel => &KESTREL,
        Model::Kestrel16 => &KESTREL16,
    }
}
