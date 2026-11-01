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
    DiskAddressLow,
    DiskAddressHigh,
    DiskAddressBank,
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
    layout[0x10] = DiskAddressLow;
    layout[0x11] = DiskAddressHigh;
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

/// The Kestrel 16's I/O page: the disk's buffer is a twenty-four-bit address, so
/// the registers above it move along by one.
pub fn kestrel16_registers() -> [Register; 32] {
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
    layout[0x10] = DiskAddressLow;
    layout[0x11] = DiskAddressHigh;
    layout[0x12] = DiskAddressBank;
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
    pub last_key: u8,
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
            last_key: 0,
        };
        bus.video.power_on(bus.ink, bus.paper);
        if profile.model == Model::Kestrel && bus.disk.present() {
            bus.disk_count = profile.disk_sectors as u16;
        }
        bus
    }

    /// A flag the way this machine says yes: all ones on the 1982 machine, the
    /// top bit on the 1992 one, which is a 65816 signalling with its sign.
    fn flag(&self, set: bool) -> u8 {
        if !set {
            return 0;
        }
        match self.profile.model {
            Model::Kestrel => 0xFF,
            Model::Kestrel16 => 0x80,
        }
    }

    /// The I/O page's answer for a register kind.
    fn read_register(&mut self, kind: Register) -> u8 {
        match kind {
            Register::KeyboardData => {
                let key = self.keyboard.read();
                if key != 0 {
                    self.last_key = key;
                }
                key
            }
            Register::KeyboardStatus => self.flag(self.keyboard.available()),
            Register::CursorColumn => self.cursor_column,
            Register::CursorRow => self.cursor_row,
            Register::Ink => self.ink,
            Register::Paper => self.paper,
            Register::Border => self.border,
            Register::Sound => 0,
            Register::FrameLow => self.frame_counter as u8,
            Register::FrameHigh => (self.frame_counter >> 8) as u8,
            Register::FrameFlag => self.flag(self.frame_flag),
            Register::DiskStatus => self.disk_status,
            Register::DiskCountLow => self.disk_count as u8,
            Register::DiskCountHigh => (self.disk_count >> 8) as u8,
            Register::Random => self.random_byte(),
            Register::LastKey => self.last_key,
            Register::Scrolled => self.flag(self.scrolled),
            Register::CursorOn => match self.profile.model {
                Model::Kestrel => u8::from(self.cursor_on),
                Model::Kestrel16 => self.flag(self.cursor_on),
            },
            Register::TapeData => self.tape.read_byte(),
            Register::TapeStatus => self.tape.status(),
            _ => 0,
        }
    }

    fn write_register(&mut self, kind: Register, value: u8) {
        match kind {
            Register::KeyboardClear => self.keyboard.clear(),
            Register::CursorColumn => {
                let last = self.profile.columns.saturating_sub(1) as u8;
                self.cursor_column = value.min(last);
            }
            Register::CursorRow => {
                let last = self.profile.rows.saturating_sub(1) as u8;
                self.cursor_row = value.min(last);
            }
            Register::Ink => self.ink = value,
            Register::Paper => self.paper = value,
            Register::Border => self.border = value,
            Register::Sound => {
                self.sound = value;
                self.notes_played += 1;
            }
            Register::FrameFlag => self.frame_flag = false,
            Register::DiskSectorLow => {
                self.disk_sector = (self.disk_sector & 0xFFFF_FF00) | u32::from(value)
            }
            Register::DiskSectorHigh => {
                self.disk_sector = (self.disk_sector & 0xFFFF_00FF) | (u32::from(value) << 8)
            }
            Register::DiskAddressLow => {
                self.disk_address = (self.disk_address & 0xFFFF_00) | u32::from(value)
            }
            Register::DiskAddressHigh => {
                self.disk_address = (self.disk_address & 0xFF_00FF) | (u32::from(value) << 8)
            }
            Register::DiskAddressBank => {
                self.disk_address = (self.disk_address & 0x00_FFFF) | (u32::from(value) << 16)
            }
            Register::DiskCountLow => {
                if self.profile.model == Model::Kestrel16 {
                    self.disk_count = (self.disk_count & 0xFF00) | u16::from(value)
                }
            }
            Register::DiskCountHigh => {
                if self.profile.model == Model::Kestrel16 {
                    self.disk_count = (self.disk_count & 0x00FF) | (u16::from(value) << 8)
                }
            }
            Register::DiskCommand => self.execute_disk_command(value),
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

    /// A command byte, executed by the controller the model has. The two are not
    /// the same device: the 1982 one moves a single sector through a sixteen-bit
    /// pointer, the 1992 one a run of them through a twenty-four-bit address, and
    /// the status bits they leave are their own.
    fn execute_disk_command(&mut self, command: u8) {
        if self.profile.model == Model::Kestrel {
            self.disk_status = 0;
            if !self.disk.present() {
                self.disk_status = 0x80;
                return;
            }
            match command {
                0 => {}
                1 => {
                    // The sector comes into RAM at the buffer pointer.
                    let mut sector = vec![0u8; self.profile.disk_sector_size as usize];
                    if !self.disk.read_sector(self.disk_sector, &mut sector) {
                        self.disk_status = 0x81;
                        return;
                    }
                    for (i, byte) in sector.iter().enumerate() {
                        let target = self.disk_address.wrapping_add(i as u32) as usize;
                        if target < self.ram.len() {
                            self.ram[target] = *byte;
                        }
                    }
                }
                2 => {
                    // And goes back out of it, zeroes and all.
                    let mut sector = vec![0u8; self.profile.disk_sector_size as usize];
                    for (i, slot) in sector.iter_mut().enumerate() {
                        let source = self.disk_address.wrapping_add(i as u32) as usize;
                        *slot = if source < self.ram.len() {
                            self.ram[source]
                        } else {
                            0
                        };
                    }
                    if !self.disk.write_sector(self.disk_sector, &sector) {
                        self.disk_status = 0x81;
                        return;
                    }
                }
                3 => self.disk_count = self.profile.disk_sectors as u16,
                _ => self.disk_status = 0x82,
            }
            return;
        }

        // The 1992 controller says "present" from the moment the machine is on,
        // so the first bit is not this command's news.
        self.disk_status = if self.disk.present() { 0x04 } else { 0 };
        if !self.disk.present() {
            self.disk_status |= 0x02;
            return;
        }
        let count = if self.disk_count == 0 { 1 } else { self.disk_count };
        match command {
            1 | 2 => {
                let mut buffer = vec![0u8; self.profile.disk_sector_size as usize];
                let mut address = self.disk_address;
                for i in 0..u32::from(count) {
                    let sector = self.disk_sector.wrapping_add(i);
                    if command == 1 {
                        if !self.disk.read_sector(sector, &mut buffer) {
                            self.disk_status |= 0x02;
                            return;
                        }
                        for (b, byte) in buffer.iter().enumerate() {
                            self.write(address.wrapping_add(b as u32), *byte);
                        }
                    } else {
                        for (b, slot) in buffer.iter_mut().enumerate() {
                            *slot = self.read(address.wrapping_add(b as u32));
                        }
                        if !self.disk.write_sector(sector, &buffer) {
                            self.disk_status |= 0x02;
                            return;
                        }
                    }
                    address = address.wrapping_add(self.profile.disk_sector_size);
                }
            }
            3 => self.disk.format("KESWARP"),
            4 => {}  // a query: the status bits are the whole answer
            _ => {
                self.disk_status |= 0x02;
                return;
            }
        }
        self.disk_status |= 0x80;  // done
    }

    /// One frame of the machine's own time. The counter and the flag are all
    /// the machine does: the interrupt is the line the ROM sees, and the work --
    /// blinking the cursor, draining the keyboard -- is the KERNAL's handler.
    pub fn tick_frame(&mut self) {
        self.frame_counter = self.frame_counter.wrapping_add(1);
        self.frame_flag = true;
    }

    /// The noise generator: one byte at a time, and deterministic, so a test that
    /// depends on RND gets the same numbers every run.
    pub fn random_byte(&mut self) -> u8 {
        let bit = (self.random_state ^ (self.random_state >> 1)) & 1;
        self.random_state = (self.random_state >> 1) | (bit << 15);
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
            // The KERNAL stands at the top of the bank, in front of the RAM under
            // it; the cartridge and the tube are cut into the middle.
            if offset >= self.profile.kernal_base {
                let index = (offset - self.profile.kernal_base) as usize;
                return self.kernal.get(index).copied().unwrap_or(0xFF);
            }
            if offset >= self.profile.cartridge_base
                && offset < self.profile.cartridge_base + 0x2000
            {
                return self
                    .cartridge
                    .as_ref()
                    .map(|rom| {
                        rom.get((offset - self.profile.cartridge_base) as usize)
                            .copied()
                            .unwrap_or(0)
                    })
                    .unwrap_or(0);
            }
            let screen_end = self.profile.screen_base + self.profile.screen_cells();
            if offset >= self.profile.screen_base && offset < screen_end {
                return self.video.cell((offset - self.profile.screen_base) as usize);
            }
            let attributes_end = self.profile.attribute_base + self.profile.screen_cells();
            if offset >= self.profile.attribute_base && offset < attributes_end {
                return self
                    .video
                    .attribute((offset - self.profile.attribute_base) as usize);
            }
            if offset >= self.profile.io_base && offset < self.profile.io_base + self.profile.io_size
            {
                let kind = self.layout[(offset as usize) & 0x1F];
                return self.read_register(kind);
            }
            if offset >= self.profile.io_base {
                return 0;
            }
            return self
                .ram
                .get(address as usize)
                .copied()
                .unwrap_or(0);
        }
        if u32::from(bank) == self.profile.basic_base >> 16 {
            let index = offset as usize;
            return self.basic.get(index).copied().unwrap_or(0xFF);
        }
        if (4..=7).contains(&bank) {
            let index = (u32::from(bank - 4) << 16 | u32::from(offset)) as usize;
            return self
                .cartridge
                .as_ref()
                .map(|rom| rom.get(index).copied().unwrap_or(0xFF))
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
