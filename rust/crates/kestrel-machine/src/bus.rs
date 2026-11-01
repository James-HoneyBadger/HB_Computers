// SPDX-License-Identifier: MIT
//
// The bus: what answers at an address, and in which order. Ported from
// `Machine::read` and `Machine::write`, which is the whole of the decode.
//
// The order matters where regions abut: on the Kestrel, RAM is asked first
// because the screen is its neighbour; on the Kestrel 16 the KERNAL is asked
// first because it stands at the top of bank zero, in front of the RAM under it.

use crate::devices::{Disk, Keyboard, Tape, Video};
use crate::profile::{Model, Profile};

/// What a register in the I/O page is. The two machines put the same devices at
/// slightly different offsets, so the layout is chosen from the profile and the
/// decoder switches on the kind rather than on the number.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Register {
    None,
    KeyboardData,
    KeyboardStatus,
    KeyboardClear,
    CursorColumn,
    CursorRow,
    Ink,
    Paper,
    Border,
    Sound,
    FrameLow,
    FrameHigh,
    FrameFlag,
    DiskSectorLow,
    DiskSectorHigh,
    DiskCommand,
    DiskStatus,
    DiskBufferLow,
    DiskBufferHigh,
    DiskCountLow,
    DiskCountHigh,
    Random,
    LastKey,
    Scrolled,
    CursorOn,
    TapeData,
    TapeStatus,
    TapeControl,
}

/// The Kestrel's I/O page, offset by offset.
pub fn kestrel_registers() -> [Register; 32] {
    use Register::*;
    let mut layout = [None; 32];
    layout[0x00] = KeyboardData;
    layout[0x01] = KeyboardStatus;
    layout[0x02] = KeyboardClear;
    layout[0x03] = CursorColumn;
    layout[0x04] = CursorRow;
    layout[0x05] = Ink;
    layout[0x06] = Paper;
    layout[0x07] = Border;
    layout[0x08] = Sound;
    layout[0x09] = FrameLow;
    layout[0x0A] = FrameHigh;
    layout[0x0B] = FrameFlag;
    layout[0x0C] = DiskSectorLow;
    layout[0x0D] = DiskSectorHigh;
    layout[0x0E] = DiskCommand;
    layout[0x0F] = DiskStatus;
    layout[0x10] = DiskBufferLow;
    layout[0x11] = DiskBufferHigh;
    layout[0x12] = DiskCountLow;
    layout[0x13] = DiskCountHigh;
    layout[0x14] = Random;
    layout[0x15] = LastKey;
    layout[0x16] = Scrolled;
    layout[0x17] = CursorOn;
    layout[0x18] = TapeData;
    layout[0x19] = TapeStatus;
    layout[0x1A] = TapeControl;
    layout
}

/// The Kestrel 16's I/O page: the disk's buffer is a 24-bit address, so the
/// registers above it move along by three.
pub fn kestrel16_registers() -> [Register; 32] {
    use Register::*;
    let mut layout = kestrel_registers();
    layout[0x10] = DiskBufferLow;
    layout[0x11] = DiskBufferHigh;
    layout[0x12] = DiskBufferHigh;
    layout[0x13] = DiskCountLow;
    layout[0x14] = DiskCountHigh;
    layout[0x15] = Random;
    layout[0x16] = LastKey;
    layout[0x17] = Scrolled;
    layout[0x18] = CursorOn;
    layout[0x19] = TapeData;
    layout[0x1A] = TapeStatus;
    layout[0x1B] = TapeControl;
    layout
}

/// The machine as the bus sees it: memory, the devices, and the state a register
/// keeps.
pub struct Bus {
    pub profile: &'static Profile,
    pub ram: Vec<u8>,
    pub video: Video,
    pub keyboard: Keyboard,
    pub disk: Disk,
    pub tape: Tape,
    pub cartridge: Option<Vec<u8>>,
    pub basic: Vec<u8>,
    pub kernal: Vec<u8>,
    layout: [Register; 32],
    pub ink: u8,
    pub paper: u8,
    pub border: u8,
    pub cursor_column: u8,
    pub cursor_row: u8,
    pub cursor_on: bool,
    pub scrolled: bool,
    pub frame_counter: u16,
    pub frame_flag: bool,
    pub sound: u8,
    pub notes_played: u32,
    pub random_state: u16,
    pub disk_sector: u32,
    pub disk_address: u32,
    pub disk_count: u16,
    pub disk_status: u8,
}

impl Bus {
    pub fn new(profile: &'static Profile) -> Self {
        let mut bus = Self {
            profile,
            ram: vec![0; profile.ram_bytes as usize],
            video: Video::new(profile.columns, profile.rows),
            keyboard: Keyboard::new(profile.keyboard_capacity),
            disk: Disk::new(profile.disk_sectors, profile.disk_sector_size),
            tape: Tape::new(),
            cartridge: None,
            basic: vec![0xFF; profile.basic_size as usize],
            kernal: vec![0xFF; profile.kernal_size as usize],
            layout: match profile.model {
                Model::Kestrel => kestrel_registers(),
                Model::Kestrel16 => kestrel16_registers(),
            },
            ink: 0x07,
            paper: 0x00,
            border: 0x09,
            cursor_column: 0,
            cursor_row: 0,
            cursor_on: profile.model == Model::Kestrel,
            scrolled: false,
            frame_counter: 0,
            frame_flag: false,
            sound: 0,
            notes_played: 0,
            random_state: 0xACE1,
            disk_sector: 0,
            disk_address: 0,
            disk_count: 0,
            disk_status: 0,
        };
        bus.video.power_on(bus.ink, bus.paper);
        bus
    }

    /// The I/O page's answer for a register kind.
    fn read_register(&mut self, kind: Register) -> u8 {
        let flag = |set: bool| -> u8 { set as u8 * 0xFF };
        match kind {
            Register::KeyboardData => {
                let key = self.keyboard.read();
                key
            }
            Register::KeyboardStatus => flag(self.keyboard.available()),
            Register::CursorColumn => self.cursor_column,
            Register::CursorRow => self.cursor_row,
            Register::Ink => self.ink,
            Register::Paper => self.paper,
            Register::Border => self.border,
            Register::Sound => 0,
            Register::FrameLow => self.frame_counter as u8,
            Register::FrameHigh => (self.frame_counter >> 8) as u8,
            Register::FrameFlag => flag(self.frame_flag),
            Register::DiskStatus => self.disk_status,
            Register::DiskCountLow => self.disk_count as u8,
            Register::DiskCountHigh => (self.disk_count >> 8) as u8,
            Register::Random => self.random_byte(),
            Register::LastKey => self.keyboard.last_key(),
            Register::Scrolled => flag(self.scrolled),
            Register::CursorOn => u8::from(self.cursor_on),
            Register::TapeData => self.tape.read_byte(),
            Register::TapeStatus => self.tape.status(),
            _ => 0,
        }
    }

    fn write_register(&mut self, kind: Register, value: u8) {
        match kind {
            Register::KeyboardClear => self.keyboard.clear(),
            Register::CursorColumn => self.cursor_column = value,
            Register::CursorRow => self.cursor_row = value,
            Register::Ink => self.ink = value,
            Register::Paper => self.paper = value,
            Register::Border => self.border = value,
            Register::Sound => {
                self.sound = value;
                self.notes_played += 1;
            }
            Register::FrameFlag => self.frame_flag = false,
            Register::DiskSectorLow => {
                self.disk_sector = (self.disk_sector & 0x1_FF00) | u32::from(value)
            }
            Register::DiskSectorHigh => {
                self.disk_sector = (self.disk_sector & 0xFF) | (u32::from(value) << 8)
            }
            Register::DiskCountLow => {
                self.disk_count = (self.disk_count & 0xFF00) | u16::from(value)
            }
            Register::DiskCountHigh => {
                self.disk_count = (self.disk_count & 0x00FF) | (u16::from(value) << 8)
            }
            Register::Scrolled => self.scrolled = false,
            Register::CursorOn => self.cursor_on = value != 0,
            Register::TapeData => self.tape.write_byte(value),
            Register::TapeControl => match value {
                1 => self.tape.play(),
                2 => self.tape.record(),
                3 => self.tape.stop(),
                4 => self.tape.rewind(),
                _ => {}
            },
            _ => {}
        }
    }

    /// The noise generator: one byte at a time, and deterministic, so a test that
    /// depends on RND gets the same numbers every run.
    pub fn random_byte(&mut self) -> u8 {
        let bit = (self.random_state ^ (self.random_state >> 2)) & 1;
        self.random_state = (self.random_state >> 1) | (bit << 14);
        self.random_state as u8
    }

    pub fn read(&mut self, address: u32) -> u8 {
        let bank = (address >> 16) as u8;
        let offset = address & 0xFFFF;

        if self.profile.model == Model::Kestrel {
            if offset < self.profile.ram_bytes {
                return self.ram[offset as usize];
            }
            let screen_end = self.profile.screen_base + self.profile.screen_cells();
            if offset >= self.profile.screen_base && offset < screen_end {
                return self.video.cell((offset - self.profile.screen_base) as usize);
            }
            if offset >= screen_end && offset < self.profile.attribute_base {
                return b' '; // the padding is screen too
            }
            let attributes_end = self.profile.attribute_base + self.profile.screen_cells();
            if offset >= self.profile.attribute_base && offset < attributes_end {
                return self.video.attribute((offset - self.profile.attribute_base) as usize);
            }
            return self.read_high(offset as u16);
        }

        if bank == 0 {
            if offset >= self.profile.kernal_base {
                return self.kernal[(offset - self.profile.kernal_base) as usize];
            }
            if offset >= self.profile.cartridge_base
                && offset < self.profile.cartridge_base + 0x2000
            {
                return self
                    .cartridge
                    .as_ref()
                    .map(|rom| rom[(offset - self.profile.cartridge_base) as usize])
                    .unwrap_or(0);
            }
            return self.read_high(offset as u16);
        }
        if u32::from(bank) == self.profile.basic_base >> 16 {
            return self.basic[offset as usize];
        }
        if bank >= 4 && bank <= 7 {
            return self
                .cartridge
                .as_ref()
                .map(|rom| rom[(offset as usize) & (rom.len().saturating_sub(1))])
                .unwrap_or(0);
        }
        self.ram
            .get(address as usize)
            .copied()
            .unwrap_or(0)
    }

    /// The regions the two machines share, in the order both decode them.
    fn read_high(&mut self, offset: u16) -> u8 {
        let screen_end = self.profile.screen_base + self.profile.screen_cells();
        if u32::from(offset) >= self.profile.screen_base && u32::from(offset) < screen_end {
            return self
                .video
                .cell((u32::from(offset) - self.profile.screen_base) as usize);
        }
        let attributes_end = self.profile.attribute_base + self.profile.screen_cells();
        if u32::from(offset) >= self.profile.attribute_base && u32::from(offset) < attributes_end {
            return self
                .video
                .attribute((u32::from(offset) - self.profile.attribute_base) as usize);
        }
        if u32::from(offset) >= self.profile.io_base
            && u32::from(offset) < self.profile.io_base + self.profile.io_size
        {
            let kind = self.layout[(offset as usize) & 0x1F];
            return self.read_register(kind);
        }
        if u32::from(offset) >= self.profile.cartridge_base
            && u32::from(offset) < self.profile.cartridge_base + self.profile.cartridge_size
        {
            return self
                .cartridge
                .as_ref()
                .map(|rom| rom[(u32::from(offset) - self.profile.cartridge_base) as usize])
                .unwrap_or(0);
        }
        if u32::from(offset) >= self.profile.basic_base
            && u32::from(offset) < self.profile.basic_base + self.profile.basic_size
        {
            return self.basic[(u32::from(offset) - self.profile.basic_base) as usize];
        }
        if u32::from(offset) >= self.profile.kernal_base {
            return self.kernal[(u32::from(offset) - self.profile.kernal_base) as usize];
        }
        0
    }

    pub fn write(&mut self, address: u32, value: u8) {
        let offset = address & 0xFFFF;
        let signed = |profile: &Profile| {
            u32::from(offset) >= profile.io_base && u32::from(offset) < profile.io_base + profile.io_size
        };
        if self.profile.model == Model::Kestrel {
            if offset < self.profile.ram_bytes {
                self.ram[offset as usize] = value;
                return;
            }
            if signed(self.profile) {
                let kind = self.layout[(offset as usize) & 0x1F];
                self.write_register(kind, value);
                return;
            }
            if u32::from(offset) >= self.profile.screen_base {
                let screen_end = self.profile.screen_base + self.profile.screen_cells();
                if u32::from(offset) < screen_end {
                    self.video
                        .set_cell((u32::from(offset) - self.profile.screen_base) as usize, value);
                    return;
                }
                let attributes_end = self.profile.attribute_base + self.profile.screen_cells();
                if u32::from(offset) >= self.profile.attribute_base
                    && u32::from(offset) < attributes_end
                {
                    self.video.set_attribute(
                        (u32::from(offset) - self.profile.attribute_base) as usize,
                        value,
                    );
                    return;
                }
            }
            return; // ROM and nothing: writes are lost
        }
        let bank = (address >> 16) as u8;
        if bank == 0 {
            if signed(self.profile) {
                let kind = self.layout[(offset as usize) & 0x1F];
                self.write_register(kind, value);
                return;
            }
            if offset < self.profile.screen_base {
                if (address as usize) < self.ram.len() {
                    self.ram[address as usize] = value;
                }
                return;
            }
            let screen_end = self.profile.screen_base + self.profile.screen_cells();
            if u32::from(offset) < screen_end {
                self.video
                    .set_cell((u32::from(offset) - self.profile.screen_base) as usize, value);
                return;
            }
            let attributes_end = self.profile.attribute_base + self.profile.screen_cells();
            if u32::from(offset) >= self.profile.attribute_base
                && u32::from(offset) < attributes_end
            {
                self.video.set_attribute(
                    (u32::from(offset) - self.profile.attribute_base) as usize,
                    value,
                );
                return;
            }
            return;
        }
        if (address as usize) < self.ram.len() {
            self.ram[address as usize] = value;
        }
    }
}
