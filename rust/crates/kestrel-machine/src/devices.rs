// SPDX-License-Identifier: MIT
//
// The four devices both machines have: the tube, the keyboard controller, the
// disk and the cassette. They know no addresses -- the machine's decode decides
// what a read or a write means and calls one of these -- which is why the same
// cassette can serve two machines with different register layouts.

use crate::profile::Profile;

/// The keys the machines name. Printable characters arrive as their own ASCII
/// code, so a value here is only for the ones that produce no character.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum SpecialKey {
    Return = 13,
    Backspace = 8,
    Escape = 27,
    Left = 28,
    Right = 29,
    Up = 30,
    Down = 31,
    Delete = 127,
}

/// A matrix of character cells, each with one attribute byte. The cells and the
/// attributes are separate arrays because both machines put them at separate
/// addresses.
pub struct Video {
    columns: u16,
    rows: u16,
    cells: Vec<u8>,
    attributes: Vec<u8>,
}

impl Video {
    pub fn new(columns: u16, rows: u16) -> Self {
        let count = usize::from(columns) * usize::from(rows);
        Self {
            columns,
            rows,
            cells: vec![b' '; count],
            attributes: vec![0; count],
        }
    }

    pub fn columns(&self) -> u16 {
        self.columns
    }

    pub fn rows(&self) -> u16 {
        self.rows
    }

    pub fn cells(&self) -> usize {
        self.cells.len()
    }

    /// The screen a machine comes up with: spaces in its ink and paper.
    pub fn power_on(&mut self, ink: u8, paper: u8) {
        self.cells.fill(b' ');
        self.attributes.fill((ink << 4) | paper);
    }

    pub fn cell(&self, index: usize) -> u8 {
        self.cells.get(index).copied().unwrap_or(0)
    }

    pub fn set_cell(&mut self, index: usize, code: u8) {
        if let Some(slot) = self.cells.get_mut(index) {
            *slot = code;
        }
    }

    pub fn attribute(&self, index: usize) -> u8 {
        self.attributes.get(index).copied().unwrap_or(0)
    }

    pub fn set_attribute(&mut self, index: usize, value: u8) {
        if let Some(slot) = self.attributes.get_mut(index) {
            *slot = value;
        }
    }

    /// One row as text, trailing spaces removed. The cursor is not part of the
    /// text: it is the host's to draw.
    pub fn line(&self, row: usize) -> String {
        if row >= usize::from(self.rows) {
            return String::new();
        }
        let start = row * usize::from(self.columns);
        let end = start + usize::from(self.columns);
        let text: String = self.cells[start..end]
            .iter()
            .map(|&code| if code == 0 { ' ' } else { char::from(code) })
            .collect();
        text.trim_end().to_string()
    }

    /// Every row, newline-separated.
    pub fn text(&self) -> String {
        // Rows separated by a newline and nothing after the last one, which is
        // what the C++ `Video::text` returns.
        let mut out = String::new();
        for row in 0..usize::from(self.rows) {
            if row != 0 {
                out.push('\n');
            }
            out.push_str(&self.line(row));
        }
        out
    }
}

/// The keyboard controller: a queue of bytes the ROM reads one at a time. A full
/// queue refuses the key rather than growing, which is what a person typing
/// faster than the machine would discover.
pub struct Keyboard {
    capacity: usize,
    queue: std::collections::VecDeque<u8>,
    last_key: u8,
}

impl Keyboard {
    pub fn new(capacity: usize) -> Self {
        Self {
            capacity,
            queue: std::collections::VecDeque::with_capacity(capacity),
            last_key: 0,
        }
    }

    pub fn push(&mut self, key: u8) -> bool {
        if self.queue.len() >= self.capacity {
            return false;
        }
        self.last_key = key;
        self.queue.push_back(key);
        true
    }

    pub fn read(&mut self) -> u8 {
        self.queue.pop_front().unwrap_or(0)
    }

    pub fn len(&self) -> usize {
        self.queue.len()
    }

    pub fn is_empty(&self) -> bool {
        self.queue.is_empty()
    }

    pub fn available(&self) -> bool {
        !self.queue.is_empty()
    }

    pub fn last_key(&self) -> u8 {
        self.last_key
    }

    pub fn clear(&mut self) {
        self.queue.clear();
    }
}

/// The disk: a store of sectors, and nothing more. The filesystem is in the ROMs.
pub struct Disk {
    sectors: u32,
    sector_size: u32,
    bytes: Vec<u8>,
    present: bool,
}

const VOLUME_NAME_OFFSET: usize = 8;
const VOLUME_NAME_LENGTH: usize = 16;

impl Disk {
    pub fn new(sectors: u32, sector_size: u32) -> Self {
        let mut disk = Self {
            sectors,
            sector_size,
            bytes: vec![0; (sectors as usize) * (sector_size as usize)],
            present: false,
        };
        disk.format("KESWARP");
        disk
    }

    pub fn sectors(&self) -> u32 {
        self.sectors
    }

    pub fn sector_size(&self) -> u32 {
        self.sector_size
    }

    pub fn present(&self) -> bool {
        self.present
    }

    pub fn insert(&mut self) {
        self.present = true;
    }

    pub fn eject(&mut self) {
        self.present = false;
    }

    /// A blank volume: every sector zero, then the name where the DOS looks.
    pub fn format(&mut self, name: &str) {
        self.bytes.fill(0);
        let bytes = name.as_bytes();
        let length = bytes.len().min(VOLUME_NAME_LENGTH);
        self.bytes[VOLUME_NAME_OFFSET..VOLUME_NAME_OFFSET + length]
            .copy_from_slice(&bytes[..length]);
        self.present = true;
    }

    pub fn volume_name(&self) -> String {
        let slice = &self.bytes[VOLUME_NAME_OFFSET..VOLUME_NAME_OFFSET + VOLUME_NAME_LENGTH];
        let end = slice.iter().position(|&b| b == 0).unwrap_or(slice.len());
        String::from_utf8_lossy(&slice[..end]).to_string()
    }

    /// Mounts a volume from the host. A volume of the wrong size is refused:
    /// half a disk is a worse lie than no disk.
    pub fn load(&mut self, bytes: &[u8]) -> bool {
        if bytes.len() != self.bytes.len() {
            return false;
        }
        self.bytes.copy_from_slice(bytes);
        self.present = true;
        true
    }

    pub fn read_sector(&self, index: u32, into: &mut [u8]) -> bool {
        let (start, end) = match self.sector_range(index) {
            Some(range) => range,
            None => return false,
        };
        if into.len() < self.sector_size as usize {
            return false;
        }
        into[..self.sector_size as usize].copy_from_slice(&self.bytes[start..end]);
        true
    }

    pub fn write_sector(&mut self, index: u32, from: &[u8]) -> bool {
        let (start, end) = match self.sector_range(index) {
            Some(range) => range,
            None => return false,
        };
        if from.len() < self.sector_size as usize {
            return false;
        }
        self.bytes[start..end].copy_from_slice(&from[..self.sector_size as usize]);
        true
    }

    fn sector_range(&self, index: u32) -> Option<(usize, usize)> {
        if index >= self.sectors {
            return None;
        }
        let start = (index as usize) * (self.sector_size as usize);
        Some((start, start + self.sector_size as usize))
    }
}

/// The cassette: a byte stream with a play head, which is all a tape ever was to
/// the machine. The block format belongs to the KERNAL.
pub struct Tape {
    bytes: Vec<u8>,
    position: usize,
    present: bool,
    mode: TapeMode,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TapeMode {
    Stopped,
    Playing,
    Recording,
}

impl Tape {
    pub fn new() -> Self {
        Self {
            bytes: Vec::new(),
            position: 0,
            present: false,
            mode: TapeMode::Stopped,
        }
    }

    pub fn load(&mut self, bytes: &[u8]) {
        self.bytes = bytes.to_vec();
        self.position = 0;
        self.present = true;
        self.mode = TapeMode::Stopped;
    }

    pub fn insert(&mut self) {
        self.present = true;
    }

    pub fn play(&mut self) {
        self.mode = TapeMode::Playing;
    }

    pub fn record(&mut self) {
        self.mode = TapeMode::Recording;
    }

    pub fn stop(&mut self) {
        self.mode = TapeMode::Stopped;
    }

    pub fn rewind(&mut self) {
        self.position = 0;
    }

    pub fn can_read(&self) -> bool {
        self.present && self.mode == TapeMode::Playing && self.position < self.bytes.len()
    }

    /// Recording writes through the play head: over what is there, and past the
    /// end when the tape runs out.
    pub fn write_byte(&mut self, value: u8) {
        if self.mode != TapeMode::Recording {
            return;
        }
        if self.position >= self.bytes.len() {
            self.bytes.push(value);
        } else {
            self.bytes[self.position] = value;
        }
        self.position += 1;
    }

    pub fn read_byte(&mut self) -> u8 {
        if !self.can_read() {
            return 0;
        }
        let value = self.bytes[self.position];
        self.position += 1;
        value
    }

    /// The status byte the ROM reads: present, playing, recording, and whether
    /// there is more to read.
    pub fn status(&self) -> u8 {
        let mut status = 0;
        if self.present {
            status |= 0x80;
        }
        if self.mode == TapeMode::Playing {
            status |= 0x40;
        }
        if self.mode == TapeMode::Recording {
            status |= 0x20;
        }
        if self.can_read() {
            status |= 0x10;
        }
        status
    }
}

impl Default for Tape {
    fn default() -> Self {
        Self::new()
    }
}

/// Everything a machine owns besides its processor and its memory.
pub struct Devices {
    pub video: Video,
    pub keyboard: Keyboard,
    pub disk: Disk,
    pub tape: Tape,
}

impl Devices {
    pub fn for_profile(profile: &Profile) -> Self {
        Self {
            video: Video::new(profile.columns, profile.rows),
            keyboard: Keyboard::new(profile.keyboard_capacity),
            disk: Disk::new(profile.disk_sectors, profile.disk_sector_size),
            tape: Tape::new(),
        }
    }
}
