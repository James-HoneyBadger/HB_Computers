// SPDX-License-Identifier: MIT
//
// The machine: one emulator with two models.
//
// The bus does the decode and holds the devices; the processor executes what the
// ROM put in memory. This module is the join: it owns both, counts the frames as
// the processor's cycles pass, and gives the host the three hands a person has --
// type at it, let it run, read the tube back.
//
// Nothing a person sees the machine do is written here. The power-on message, the
// prompt, the error messages and the arithmetic are all machine code in a ROM
// image; this file only decides who runs and for how long.

use std::path::Path;

use crate::bus::Bus as MachineBus;
use crate::cpu6502::{self, Cpu6502};
use crate::cpu65816::{self, Cpu65816};
use crate::profile::Model;

/// The keyboard's return key, and the other keys that are not characters.
pub const KEY_RETURN: u8 = 0x0D;

impl cpu6502::Bus for MachineBus {
    fn read(&mut self, address: u16) -> u8 {
        MachineBus::read(self, u32::from(address))
    }

    fn write(&mut self, address: u16, value: u8) {
        MachineBus::write(self, u32::from(address), value);
    }
}

impl cpu65816::Bus for MachineBus {
    fn read(&mut self, address: u32) -> u8 {
        MachineBus::read(self, address)
    }

    fn write(&mut self, address: u32, value: u8) {
        MachineBus::write(self, address, value);
    }
}

/// Either processor, since a machine has exactly one.
pub enum Cpu {
    Mos6502(Cpu6502),
    Mos65816(Cpu65816),
}

pub struct Machine {
    pub bus: MachineBus,
    pub cpu: Cpu,
    cycles_to_frame: u32,
}

/// Why a ROM could not be put in the machine.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RomError {
    /// The image is not the size the machine's socket takes.
    WrongSize { wanted: usize, got: usize },
    /// The file could not be read.
    Unreadable(String),
}

impl std::fmt::Display for RomError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            RomError::WrongSize { wanted, got } => {
                write!(f, "a ROM of {wanted} bytes was needed, {got} were given")
            }
            // A ROM is an image and so is a disk; the sentence has to serve both.
            RomError::Unreadable(why) => write!(f, "{why}"),
        }
    }
}

impl Cpu {
    /// One instruction, or the interrupt if one is due. The line is recomputed
    /// before every instruction because it is level-triggered: it is high while a
    /// key waits or a frame has passed, and drops when the ROM has dealt with
    /// both.
    fn step(&mut self, bus: &mut MachineBus, irq: bool) -> u32 {
        match self {
            Cpu::Mos6502(cpu) => {
                cpu.set_irq_line(irq);
                cpu.step(&mut *bus)
            }
            Cpu::Mos65816(cpu) => {
                cpu.set_irq_line(irq);
                cpu.step(&mut *bus)
            }
        }
    }

    fn reset(&mut self, bus: &mut MachineBus) {
        match self {
            Cpu::Mos6502(cpu) => cpu.reset(&mut *bus),
            Cpu::Mos65816(cpu) => cpu.reset(&mut *bus),
        }
    }

    fn request_nmi(&mut self) {
        match self {
            Cpu::Mos6502(cpu) => cpu.request_nmi(),
            Cpu::Mos65816(cpu) => cpu.request_nmi(),
        }
    }

    pub fn instructions(&self) -> u64 {
        match self {
            Cpu::Mos6502(cpu) => cpu.instruction_count(),
            Cpu::Mos65816(cpu) => cpu.instruction_count(),
        }
    }

    pub fn cycles(&self) -> u64 {
        match self {
            Cpu::Mos6502(cpu) => cpu.cycle_count(),
            Cpu::Mos65816(cpu) => cpu.cycle_count(),
        }
    }
}

impl Machine {
    /// A machine, powered on with empty ROM sockets: the bus and the processor
    /// are real, the language is not there yet. `mount_shipped_roms` is what a
    /// caller that wants a working machine does next.
    pub fn new(model: Model) -> Self {
        let bus = MachineBus::new(profile_of(model));
        let cpu = match model {
            Model::Kestrel => Cpu::Mos6502(Cpu6502::new()),
            Model::Kestrel16 => Cpu::Mos65816(Cpu65816::new()),
        };
        Self {
            bus,
            cpu,
            cycles_to_frame: 0,
        }
    }

    pub fn model(&self) -> Model {
        self.bus.profile.model
    }

    /// The KERNAL: the part of the machine that is always there. Its size is the
    /// profile's, and an image of the wrong size is refused.
    pub fn mount_kernal(&mut self, image: &[u8]) -> Result<(), RomError> {
        if image.len() != self.bus.kernal.len() {
            return Err(RomError::WrongSize {
                wanted: self.bus.kernal.len(),
                got: image.len(),
            });
        }
        self.bus.kernal.copy_from_slice(image);
        Ok(())
    }

    /// The language: a socket ROM on the Kestrel, a bank of its own on the
    /// Kestrel 16.
    pub fn mount_basic(&mut self, image: &[u8]) -> Result<(), RomError> {
        if image.len() != self.bus.basic.len() {
            return Err(RomError::WrongSize {
                wanted: self.bus.basic.len(),
                got: image.len(),
            });
        }
        self.bus.basic.copy_from_slice(image);
        Ok(())
    }

    /// Loads the images assembled from `roms/` by the ported assemblers, so the
    /// machine needs no build step and no artefact from the C++ tree.
    pub fn mount_assembled_roms(&mut self) -> Result<(), RomError> {
        let roms = crate::roms::shipped(self.model()).map_err(RomError::Unreadable)?;
        self.mount_kernal(&roms.kernal.clone())?;
        self.mount_basic(&roms.basic.clone())
    }

    /// Loads the images the C++ build assembled. `root` is the build's
    /// `generated` directory, which holds `kestrel/` and `kestrel16/`.
    pub fn mount_shipped_roms(&mut self, root: &Path) -> Result<(), RomError> {
        let directory = root.join(match self.model() {
            Model::Kestrel => "kestrel",
            Model::Kestrel16 => "kestrel16",
        });
        let kernal = read_image(&directory.join("kernal.bin"))?;
        let basic = read_image(&directory.join("basic.bin"))?;
        self.mount_kernal(&kernal)?;
        self.mount_basic(&basic)
    }

    /// A disk in the drive: the image has to be the whole volume, and a file of
    /// any other size is refused rather than half-loaded.
    pub fn mount_disk_image(&mut self, path: &Path) -> Result<(), RomError> {
        let bytes = read_image(path)?;
        let wanted = self.bus.disk.image_size();
        if bytes.len() != wanted {
            return Err(RomError::WrongSize {
                wanted,
                got: bytes.len(),
            });
        }
        self.bus.disk.load(&bytes);
        Ok(())
    }

    /// Write the volume back out, which is what a disk in a drive means: what the
    /// machine wrote is there the next time it is mounted.
    pub fn save_disk_image(&self, path: &Path) -> std::io::Result<()> {
        std::fs::write(path, self.bus.disk.image())
    }

    /// A tape in the deck. A tape is a byte stream, so any length is a tape.
    pub fn mount_tape_image(&mut self, path: &Path) -> Result<(), RomError> {
        let bytes = read_image(path)?;
        self.bus.tape.load(&bytes);
        self.bus.tape.insert();
        Ok(())
    }

    pub fn save_tape_image(&self, path: &Path) -> std::io::Result<()> {
        std::fs::write(path, self.bus.tape.image())
    }

    /// Power-on: RAM and the tube cleared, the devices idle, the clock at zero.
    /// The ROMs are not touched -- they are read-only and the machine is
    /// switched on with them already in.
    pub fn reset(&mut self) {
        for byte in &mut self.bus.ram {
            *byte = 0;
        }
        let (ink, paper) = (self.bus.ink, self.bus.paper);
        self.bus.video.power_on(ink, paper);
        self.bus.keyboard.clear();
        self.bus.cursor_column = 0;
        self.bus.cursor_row = 0;
        self.bus.cursor_on = self.bus.profile.model == Model::Kestrel;
        self.bus.scrolled = false;
        self.bus.frame_counter = 0;
        self.bus.frame_flag = false;
        self.cycles_to_frame = 0;
        self.bus.sound = 0;
        self.bus.notes_played = 0;
        self.bus.random_state = 0xACE1;
        self.bus.last_key = 0;
        self.bus.disk_sector = 0;
        self.bus.disk_address = 0;
        // The Kestrel's count register reports the volume's size, so it answers
        // from the moment the machine is switched on.
        self.bus.disk_count = if self.bus.profile.model == Model::Kestrel && self.bus.disk.present()
        {
            self.bus.profile.disk_sectors as u16
        } else {
            0
        };
        self.bus.disk_status = 0;

        let Machine { bus, cpu, .. } = self;
        cpu.reset(bus);
    }

    /// Run the processor, and count frames as its cycles pass.
    pub fn run_cycles(&mut self, cycles: i64) {
        let per_frame = self.bus.profile.cycles_per_frame;
        let machine = self;
        let mut done = 0;
        while done < cycles {
            let irq = machine.bus.keyboard.available() || machine.bus.frame_flag;
            let taken = machine.cpu.step(&mut machine.bus, irq);
            done += i64::from(taken);
            machine.cycles_to_frame += taken;
            if machine.cycles_to_frame >= per_frame {
                machine.cycles_to_frame -= per_frame;
                machine.bus.tick_frame();
            }
        }
    }

    pub fn run_frames(&mut self, frames: i32) {
        let per_frame = i64::from(self.bus.profile.cycles_per_frame);
        self.run_cycles(per_frame * i64::from(frames));
    }

    pub fn type_key(&mut self, key: u8) {
        self.bus.keyboard.push(key);
    }

    pub fn type_text(&mut self, text: &str) {
        for byte in text.bytes() {
            self.type_key(byte);
        }
    }

    pub fn type_line(&mut self, text: &str) {
        self.type_text(text);
        self.type_key(KEY_RETURN);
    }

    /// A line typed at the speed of the emulated machine rather than the host's.
    /// The keyboard controller holds a fixed number of keys, and a line pushed in
    /// at once overflows it and loses characters -- including the Return, which
    /// welds two lines together.
    pub fn type_line_paced(&mut self, text: &str) {
        for byte in text.bytes() {
            while self.bus.keyboard.len() >= 8 {
                self.run_frames(1);
            }
            self.type_key(byte);
            self.run_frames(2);
        }
        while self.bus.keyboard.len() >= 8 {
            self.run_frames(1);
        }
        self.type_key(KEY_RETURN);
        self.run_frames(2);
        while !self.bus.keyboard.is_empty() {
            self.run_frames(1);
        }
        self.run_frames(20);
    }

    /// The BREAK key: a non-maskable interrupt, which the KERNAL turns into a
    /// warm start.
    pub fn press_break(&mut self) {
        self.cpu.request_nmi();
    }

    /// The tube, as text.
    pub fn screen_text(&self) -> String {
        self.bus.video.text()
    }

    pub fn screen_line(&self, row: usize) -> String {
        self.bus.video.line(row)
    }
}

fn profile_of(model: Model) -> &'static crate::profile::Profile {
    match model {
        Model::Kestrel => &crate::profile::KESTREL,
        Model::Kestrel16 => &crate::profile::KESTREL16,
    }
}

/// Where the C++ build leaves the assembled images. `chosen` wins; then
/// `KESTREL_ROMS`; then `build/generated` under the current directory or under
/// the repository this crate lives in. A checkout that has never been built has
/// none, and saying so is more use than a missing-file error.
pub fn shipped_roms_directory(chosen: Option<&Path>) -> Result<std::path::PathBuf, String> {
    if let Some(path) = chosen {
        return Ok(path.to_path_buf());
    }
    if let Some(path) = std::env::var_os("KESTREL_ROMS") {
        return Ok(std::path::PathBuf::from(path));
    }
    let candidates = [
        std::path::PathBuf::from("build/generated"),
        Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../build/generated"),
    ];
    for candidate in candidates {
        if candidate.join("kestrel").join("kernal.bin").is_file() {
            return Ok(candidate);
        }
    }
    Err("no assembled ROMs: run ./start.sh build, or say where they are with --roms".to_string())
}

fn read_image(path: &Path) -> Result<Vec<u8>, RomError> {
    std::fs::read(path).map_err(|error| RomError::Unreadable(format!("{}: {error}", path.display())))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn boot(model: Model) -> Machine {
        let mut machine = Machine::new(model);
        machine
            .mount_assembled_roms()
            .expect("the ROM sources to assemble");
        machine.reset();
        machine.run_frames(25);
        machine
    }

    #[test]
    fn the_1982_machine_boots_and_answers() {
        let mut machine = boot(Model::Kestrel);
        assert!(
            machine.screen_text().contains("KESTREL BASIC"),
            "the power-on message, from the ROM:\n{}",
            machine.screen_text()
        );
        machine.type_line_paced("PRINT 2+2*3");
        assert!(
            machine.screen_text().lines().any(|row| row.trim() == "8"),
            "two and two threes is eight:\n{}",
            machine.screen_text()
        );
        // Nothing ran off the rails: every opcode the ROM used is a documented one.
        match machine.cpu {
            Cpu::Mos6502(ref cpu) => assert_eq!(cpu.illegal_opcodes(), 0),
            _ => panic!("the 1982 machine has a 6502"),
        }
    }

    #[test]
    fn the_1982_machine_stores_and_runs_a_program() {
        let mut machine = boot(Model::Kestrel);
        machine.type_line_paced("10 FOR I=1 TO 3");
        machine.type_line_paced("20 PRINT I*I");
        machine.type_line_paced("30 NEXT I");
        machine.type_line_paced("RUN");
        machine.run_frames(120);
        let screen = machine.screen_text();
        let rows: Vec<&str> = screen.lines().map(str::trim).collect();
        for wanted in ["1", "4", "9"] {
            assert!(rows.contains(&wanted), "no {wanted} in:\n{screen}");
        }
    }

    #[test]
    fn the_1992_machine_boots_and_answers() {
        let mut machine = boot(Model::Kestrel16);
        machine.type_line_paced("PRINT 1000*20");
        assert!(
            machine.screen_text().lines().any(|row| row.trim() == "20000"),
            "sixteen bits, not eight:\n{}",
            machine.screen_text()
        );
        match machine.cpu {
            Cpu::Mos65816(ref cpu) => assert_eq!(cpu.illegal_opcodes(), 0),
            _ => panic!("the 1992 machine has a 65816"),
        }
    }

    /// A disk in the drive is a file on the host, and what the machine wrote to
    /// it is there when it is mounted again. The controller has no DOS on it yet,
    /// so the write is made through the device rather than through BASIC.
    #[test]
    fn a_disk_image_goes_in_and_comes_back_out() {
        let mut machine = boot(Model::Kestrel);
        let size = machine.bus.disk.image_size();
        assert_eq!(size, 512 * 256, "the 1982 drive is 512 sectors of 256");

        let path = std::env::temp_dir().join("kestrel-disk-image-test.dsk");
        let mut image = vec![0u8; size];
        image[0] = 0x42;
        std::fs::write(&path, &image).expect("a disk image to write");

        machine.mount_disk_image(&path).expect("a disk that fits the drive");
        assert_eq!(machine.bus.disk.image()[0], 0x42, "the image went in");

        let mut sector = vec![0u8; 256];
        assert!(machine.bus.disk.read_sector(0, &mut sector));
        assert_eq!(sector[0], 0x42);
        sector[0] = 0x99;
        assert!(machine.bus.disk.write_sector(0, &sector));
        machine.save_disk_image(&path).expect("the disk to be written back");

        let again = std::fs::read(&path).expect("the disk image to be read back");
        assert_eq!(again[0], 0x99, "what the machine wrote survived");

        // A file of the wrong size is refused, not half-loaded.
        std::fs::write(&path, vec![0u8; 100]).expect("a short file");
        assert!(matches!(
            machine.mount_disk_image(&path),
            Err(RomError::WrongSize { wanted, got }) if wanted == size && got == 100
        ));
        let _ = std::fs::remove_file(&path);
    }

    /// A tape is a byte stream, so any length is a tape, and it comes back out
    /// byte for byte.
    /// The whole point of a virtual disk: a program saved on one machine is
    /// found by a second machine that mounts the same file. This runs the DOS in
    /// the ROM -- `SAVE`, `CATALOG`, `NEW`, `LOAD` -- and then boots a fresh
    /// machine from the image to prove the file outlived the first one.
    /// The first service of Kestrel DOS 2.0: `FORMAT` writes a volume header, and
    /// the file on the host is what says so.
    #[test]
    fn the_1992_machine_formats_a_volume() {
        let path = std::env::temp_dir().join("kestrel-dos2-format.dsk");
        std::fs::write(&path, vec![0u8; 4096 * 512]).expect("a blank volume");

        let mut machine = boot(Model::Kestrel16);
        machine.mount_disk_image(&path).expect("a disk in the drive");
        machine.type_line_paced("FORMAT");
        machine.run_frames(40);
        machine.save_disk_image(&path).expect("the volume written back");

        let image = std::fs::read(&path).expect("the volume");
        assert_eq!(&image[0..7], b"HBDOS20", "the signature a version can recognise");
        assert_eq!(image[7], 0, "and the zero that ends it");
        assert_eq!(&image[8..24], &[32u8; 16], "a name of spaces, for now");
        assert_eq!(
            u16::from(image[24]) | (u16::from(image[25]) << 8),
            4096,
            "the sector count, low byte first"
        );
        assert_eq!(image[32] & 1, 1, "sector zero is the header, and it is taken");
        assert_eq!(image[33], 0, "everything else is free");
        let _ = std::fs::remove_file(&path);
    }

    #[test]
    fn a_saved_program_outlives_the_machine_that_saved_it() {
        let path = std::env::temp_dir().join("kestrel-dos-round-trip.dsk");
        let size = 512 * 256;
        std::fs::write(&path, vec![0u8; size]).expect("a blank volume");

        let mut machine = boot(Model::Kestrel);
        machine.mount_disk_image(&path).expect("a disk in the drive");
        machine.type_line_paced("10 PRINT \"SAVED OK\"");
        machine.type_line_paced("SAVE \"TEST\"");
        machine.run_frames(40);
        machine.save_disk_image(&path).expect("the volume written back");

        // A different machine, the same disk.
        let mut again = boot(Model::Kestrel);
        again.mount_disk_image(&path).expect("the saved volume");
        again.type_line_paced("CATALOG");
        assert!(
            again.screen_text().contains("TEST"),
            "the catalogue knows the file:\n{}",
            again.screen_text()
        );
        again.type_line_paced("NEW");
        again.type_line_paced("LOAD \"TEST\"");
        again.type_line_paced("RUN");
        assert!(
            again.screen_text().lines().any(|row| row.trim() == "SAVED OK"),
            "and the program still runs:\n{}",
            again.screen_text()
        );
        let _ = std::fs::remove_file(&path);
    }

    #[test]
    fn a_tape_image_goes_in_and_comes_back_out() {
        let mut machine = boot(Model::Kestrel16);
        let path = std::env::temp_dir().join("kestrel-tape-image-test.tap");
        std::fs::write(&path, [1u8, 2, 3, 4]).expect("a tape image to write");
        machine.mount_tape_image(&path).expect("any length is a tape");
        assert_eq!(machine.bus.tape.image(), &[1, 2, 3, 4]);
        machine.save_tape_image(&path).expect("the tape to be written back");
        assert_eq!(std::fs::read(&path).expect("the tape back"), vec![1, 2, 3, 4]);
        let _ = std::fs::remove_file(&path);
    }

    #[test]
    fn a_rom_of_the_wrong_size_is_refused() {
        let mut machine = Machine::new(Model::Kestrel);
        let short = vec![0u8; 100];
        assert_eq!(
            machine.mount_kernal(&short),
            Err(RomError::WrongSize { wanted: 8192, got: 100 })
        );
        assert_eq!(
            machine.mount_basic(&short),
            Err(RomError::WrongSize {
                wanted: 16384,
                got: 100
            })
        );
    }

    /// The two machines prompt the same way: `READY.` on a line of its own, and
    /// what a person types on the line beneath it. The 1992 machine used to say
    /// `READY> ` and take its input on the same line, which made a session look
    /// like a different computer from the one the manual describes.
    #[test]
    fn both_machines_prompt_the_same_way() {
        for model in [Model::Kestrel, Model::Kestrel16] {
            let mut machine = boot(model);
            machine.type_line_paced("PRINT 2+2*3");
            let rows: Vec<String> = (0..machine.bus.video.rows() as usize)
                .map(|row| machine.screen_line(row).trim().to_string())
                .collect();
            let ready = rows
                .iter()
                .position(|row| row == "READY.")
                .unwrap_or_else(|| panic!("no READY. on {model:?}:\n{}", machine.screen_text()));
            assert_eq!(rows[ready + 1], "PRINT 2+2*3", "the line is typed beneath it");
            assert_eq!(rows[ready + 2], "8", "and answered beneath that");
            assert!(
                !machine.screen_text().contains("READY>"),
                "the old prompt survived on {model:?}:\n{}",
                machine.screen_text()
            );
        }
    }

    /// A statement ends its line unless something said otherwise. The 1992
    /// BASIC held the line open after `;` and never closed it again once a value
    /// had been printed, so `PRINT "X ";6*7` ran into the prompt -- the extra
    /// newline the old prompt printed was hiding it.
    #[test]
    fn a_print_statement_finishes_its_line() {
        let mut machine = boot(Model::Kestrel16);
        machine.type_line_paced("PRINT \"X \";6*7");
        machine.type_line_paced("PRINT 1,2");
        machine.type_line_paced("PRINT \"A\"");
        let rows: Vec<String> = (0..machine.bus.video.rows() as usize)
            .map(|row| machine.screen_line(row).trim_end().to_string())
            .collect();
        assert!(rows.iter().any(|row| row == "X 42"), "the value ended its line:\n{}", machine.screen_text());
        assert!(rows.iter().any(|row| row == "1 2"), "a comma continues the line:\n{}", machine.screen_text());
        assert!(rows.iter().any(|row| row == "A"), "a string ends its line:\n{}", machine.screen_text());
        assert!(
            !machine.screen_text().lines().any(|row| row.trim_end().ends_with("READY.") && row.trim() != "READY."),
            "nothing ran into the prompt:\n{}",
            machine.screen_text()
        );
    }

    #[test]
    fn the_frame_is_the_profile_s() {
        let mut machine = boot(Model::Kestrel);
        let before = machine.bus.frame_counter;
        machine.run_frames(10);
        assert!(
            machine.bus.frame_counter >= before + 9,
            "ten frames of twenty thousand cycles"
        );
        // The 1992 machine's frame is eight times longer, and so is the count of
        // instructions in it.
        let mut kestrel16 = boot(Model::Kestrel16);
        let start = kestrel16.bus.frame_counter;
        kestrel16.run_frames(1);
        assert_eq!(kestrel16.bus.frame_counter, start + 1);
    }
}
