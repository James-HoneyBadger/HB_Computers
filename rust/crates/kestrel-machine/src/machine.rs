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
    /// `CATALOG` reads the volume header back through the DOS and prints the
    /// name it finds, which is the first time the 1992 machine has read anything
    /// it wrote to a disk.
    #[test]
    fn the_1992_machine_reads_its_volume_back() {
        let path = std::env::temp_dir().join("kestrel-dos2-catalog.dsk");
        std::fs::write(&path, vec![0u8; 4096 * 512]).expect("a blank volume");

        let mut machine = boot(Model::Kestrel16);
        machine.mount_disk_image(&path).expect("a disk in the drive");
        machine.type_line_paced("FORMAT");
        machine.type_line_paced("CATALOG");
        machine.run_frames(40);

        let screen = machine.screen_text();
        assert!(
            screen.lines().any(|row| row.trim() == "[                ]"),
            "the volume name came back off the disk:\n{screen}"
        );
        let _ = std::fs::remove_file(&path);
    }

    /// A program is a run of records in bank zero, and its text crosses a page
    /// every two hundred-odd bytes. The text pointer is stepped in the eight-bit
    /// mode the language runs in between statements, where `INC` moves the low
    /// byte and carries into nothing: a line that straddled a page boundary put
    /// the pointer back at the top of the page and the machine read it for ever.
    ///
    /// Line ten is 243 bytes -- two for its number, 240 of remark and a
    /// terminator -- so line twenty's text begins at `$08F5` and its string
    /// runs over `$0900`.
    #[test]
    fn a_1992_line_that_crosses_a_page_still_runs() {
        let mut machine = boot(Model::Kestrel16);
        machine.type_line_paced(&format!("10 REM {}", "P".repeat(236)));
        machine.type_line_paced("20 PRINT \"ACROSS THE PAGE BOUNDARY\"");
        machine.type_line_paced("30 PRINT 6*7");
        machine.type_line_paced("RUN");
        machine.run_frames(40);

        let screen = machine.screen_text();
        assert!(
            screen
                .lines()
                .any(|row| row.trim() == "ACROSS THE PAGE BOUNDARY"),
            "the string on the far side of the boundary printed:\n{screen}"
        );
        assert!(
            screen.lines().any(|row| row.trim() == "42"),
            "and the line after it ran:\n{screen}"
        );
        assert!(
            !screen.contains("?WHAT?"),
            "nothing in that program was unreadable:\n{screen}"
        );
    }

    /// `FOR I=5 TO 1` has no passes in it: the body is skipped to the matching
    /// `NEXT` and no frame is pushed. The 1982 machine always answered this way
    /// and the manual says so; this machine used to run the body once.
    #[test]
    fn a_1992_loop_past_its_limit_runs_no_times() {
        let mut machine = boot(Model::Kestrel16);
        for line in [
            "10 FOR I=5 TO 1",
            "20 PRINT \"BODY\"",
            "30 NEXT I",
            "40 PRINT \"AFTER\"",
            "RUN",
        ] {
            machine.type_line_paced(line);
        }
        machine.run_frames(40);

        let screen = machine.screen_text();
        assert!(
            screen.lines().any(|row| row.trim() == "AFTER"),
            "the line after the loop ran:\n{screen}"
        );
        assert!(
            !screen.lines().any(|row| row.trim() == "BODY"),
            "and the body did not -- the line was typed, but never printed:\n{screen}"
        );
    }

    /// The skipped body is found by reading the program as text, so the scan has
    /// to know a nested `FOR` from a `NEXT`, a quoted literal from a statement,
    /// and a remark from either. One program covers all four: the outer loop
    /// never runs, and everything it must step over is in its body.
    #[test]
    fn a_1992_skipped_loop_finds_its_own_next() {
        let mut machine = boot(Model::Kestrel16);
        for line in [
            "10 FOR I=1 TO 0",
            "20 FOR J=1 TO 3",
            "30 PRINT \"NEVER\"",
            "40 NEXT J",
            "50 PRINT \"FOR NEXT IN A STRING\"",
            "60 REM A REMARK ABOUT NEXT AND FOR",
            "70 NEXT I",
            "80 FOR K=1 TO 0: PRINT \"NEVER\"; : NEXT K: PRINT \"ONE LINE\"",
            "90 PRINT \"FOUND IT\"",
            "RUN",
        ] {
            machine.type_line_paced(line);
        }
        machine.run_frames(40);

        let screen = machine.screen_text();
        assert!(
            screen.lines().any(|row| row.trim() == "FOUND IT"),
            "the scan found the outer loop's NEXT:\n{screen}"
        );
        assert!(
            screen.lines().any(|row| row.trim() == "ONE LINE"),
            "and went on with the line the NEXT was on:\n{screen}"
        );
        assert!(
            !screen.lines().any(|row| row.trim().contains("NEVER") && !row.contains("PRINT")),
            "nothing in either body ran:\n{screen}"
        );
    }

    /// A `FOR` with no `NEXT` cannot skip its body, and says which mistake it is.
    #[test]
    fn a_1992_loop_without_a_next_says_so() {
        let mut machine = boot(Model::Kestrel16);
        for line in ["10 FOR I=1 TO 0", "20 PRINT \"NEVER\"", "RUN"] {
            machine.type_line_paced(line);
        }
        machine.run_frames(40);

        let screen = machine.screen_text();
        assert!(
            screen.contains("?FOR WITHOUT NEXT"),
            "the machine names the mistake:\n{screen}"
        );
        assert!(
            !screen
                .lines()
                .any(|row| row.trim().contains("NEVER") && !row.contains("PRINT")),
            "and does not run the body:\n{screen}"
        );
    }

    /// A colon separates statements, and `PRINT` is one of them: it stops at the
    /// colon instead of trying to read it as another item to print, which is
    /// what used to happen, so `PRINT "A": PRINT "B"` said `A?WHAT?B`.
    #[test]
    fn a_1992_colon_separates_statements() {
        let mut machine = boot(Model::Kestrel16);
        machine.type_line_paced("PRINT 1+1: PRINT \"C\"");
        machine.type_line_paced("LET A=7: PRINT A");
        machine.type_line_paced("FOR I=1 TO 3: PRINT I;: NEXT I");
        machine.run_frames(40);

        let screen = machine.screen_text();
        assert!(
            screen.lines().any(|row| row.trim() == "2"),
            "the value before the colon printed:\n{screen}"
        );
        assert!(
            screen.lines().any(|row| row.trim() == "C"),
            "and so did the statement after it:\n{screen}"
        );
        assert!(
            screen.lines().any(|row| row.trim() == "7"),
            "an assignment ends at the colon too:\n{screen}"
        );
        assert!(
            screen.lines().any(|row| row.starts_with("123")),
            "and a whole loop on one line runs, which the prompt can hold:\n{screen}"
        );
        assert!(!screen.contains("?WHAT?"), "nothing there was unreadable:\n{screen}");
    }

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
        assert_eq!(
            &image[512..1024],
            &[0u8; 512][..],
            "sector one is an empty directory"
        );
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

    /// The same round trip on the 1992 machine, through Kestrel DOS 2.0: a volume
    /// is formatted, a program is saved to it, and a second machine that has never
    /// seen the program reads the catalogue, loads the file and runs it. The
    /// deletion at the end is checked against the volume itself, because a screen
    /// that has already shown the name cannot say it is gone.
    #[test]
    fn a_1992_program_outlives_the_machine_that_saved_it() {
        let path = std::env::temp_dir().join("kestrel16-dos-round-trip.dsk");
        std::fs::write(&path, vec![0u8; 4096 * 512]).expect("a blank volume");

        let mut machine = boot(Model::Kestrel16);
        machine.mount_disk_image(&path).expect("a disk in the drive");
        machine.type_line_paced("FORMAT");
        machine.type_line_paced("10 PRINT \"SAVED OK\"");
        machine.type_line_paced("20 PRINT 6*7");
        machine.type_line_paced("SAVE \"TEST\"");
        machine.run_frames(40);
        assert!(
            machine.screen_text().contains("SAVED"),
            "the machine says the save happened:\n{}",
            machine.screen_text()
        );
        machine.save_disk_image(&path).expect("the volume written back");

        // A different machine, the same disk.
        let mut again = boot(Model::Kestrel16);
        again.mount_disk_image(&path).expect("the saved volume");
        again.type_line_paced("CATALOG");
        let screen = again.screen_text();
        assert!(
            screen.lines().any(|row| row.trim() == "TEST"),
            "the catalogue knows the file:\n{screen}"
        );
        assert!(
            !screen.contains("EMPTY"),
            "and does not call a volume with a file on it empty:\n{screen}"
        );

        again.type_line_paced("NEW");
        again.type_line_paced("LIST");
        assert!(
            !again.screen_text().lines().any(|row| row.contains("SAVED OK")),
            "NEW throws the program away before LOAD is asked for it:\n{}",
            again.screen_text()
        );
        again.type_line_paced("LOAD \"TEST\"");
        again.type_line_paced("RUN");
        let screen = again.screen_text();
        assert!(
            screen.lines().any(|row| row.trim() == "SAVED OK"),
            "the program came back off the disk and ran:\n{screen}"
        );
        assert!(
            screen.lines().any(|row| row.trim() == "42"),
            "and ran all of it:\n{screen}"
        );

        again.type_line_paced("DELETE \"TEST\"");
        again.run_frames(40);
        again.save_disk_image(&path).expect("the deletion written back");
        let image = std::fs::read(&path).expect("the volume");
        assert_eq!(
            &image[512..528],
            &[0u8; 16][..],
            "DELETE empties the entry, so the name is gone"
        );
        assert_eq!(
            image[32], 0b0000_0011,
            "and gives the file's sector back to the map, which holds the header\n\
             (bit 0) and the directory (bit 1) and nothing else"
        );
        let _ = std::fs::remove_file(&path);
    }

    /// The allocator walks the free map a sector at a time, and the map routine
    /// hands back the *bit's* number in X rather than the sector's. A walk that
    /// counted in X lost its place at sector eight, where the bit is nought
    /// again: the seventh file found no room on a volume with two megabytes
    /// free, and the ones after it were written over the volume header. Seven
    /// one-sector files take the volume past that boundary.
    #[test]
    fn a_1992_volume_fills_past_its_first_page() {
        let path = std::env::temp_dir().join("kestrel16-dos-fill.dsk");
        std::fs::write(&path, vec![0u8; 4096 * 512]).expect("a blank volume");

        let mut machine = boot(Model::Kestrel16);
        machine.mount_disk_image(&path).expect("a disk in the drive");
        machine.type_line_paced("FORMAT");
        let names = ["AAA", "BBB", "CCC", "DDD", "EEE", "FFF", "GGG"];
        for name in names {
            machine.type_line_paced(&format!("10 PRINT \"{name}\""));
            machine.type_line_paced(&format!("SAVE \"{name}\""));
            machine.type_line_paced("NEW");
        }
        machine.run_frames(80);
        machine.save_disk_image(&path).expect("the volume written back");

        let image = std::fs::read(&path).expect("the volume");
        let entries: Vec<(String, u16, u16)> = (0..names.len())
            .map(|entry| {
                let at = 512 + entry * 32;
                let name = String::from_utf8_lossy(&image[at..at + 16]).trim_end().to_string();
                let first = u16::from(image[at + 16]) | (u16::from(image[at + 17]) << 8);
                let count = u16::from(image[at + 18]) | (u16::from(image[at + 19]) << 8);
                (name, first, count)
            })
            .collect();
        let wanted: Vec<(String, u16, u16)> = names
            .iter()
            .enumerate()
            .map(|(index, name)| (name.to_string(), 2 + index as u16, 1))
            .collect();
        assert_eq!(
            entries, wanted,
            "each file gets the next free sector, past the volume's first page"
        );
        assert_eq!(
            &image[32..34],
            &[0b1111_1111, 0b0000_0001],
            "and the map says the nine sectors they hold are taken"
        );

        // the last file loads and runs, so the sector it names is its own
        machine.type_line_paced("LOAD \"GGG\"");
        machine.type_line_paced("RUN");
        machine.run_frames(40);
        assert!(
            machine.screen_text().lines().any(|row| row.trim() == "GGG"),
            "the file at sector eight came back and ran:\n{}",
            machine.screen_text()
        );
        let _ = std::fs::remove_file(&path);
    }

    /// The filesystem walks the program store a byte at a time, and a program
    /// bigger than a sector makes that walk pass a page boundary -- where an
    /// eight-bit `INC` on the pointer starts the page again, so the save wrote
    /// the same bytes twice and the load read them twice. This program is four
    /// sectors, and the last line's answer is only there if all of it came back.
    #[test]
    fn a_1992_program_larger_than_a_sector_round_trips() {
        let path = std::env::temp_dir().join("kestrel16-dos-big.dsk");
        std::fs::write(&path, vec![0u8; 4096 * 512]).expect("a blank volume");

        let mut machine = boot(Model::Kestrel16);
        machine.mount_disk_image(&path).expect("a disk in the drive");
        machine.type_line_paced("FORMAT");
        for i in 1..=40 {
            machine.type_line_paced(&format!("{i}0 PRINT \"LINE {i}\""));
        }
        machine.type_line_paced("500 PRINT 6*7");
        machine.type_line_paced("SAVE \"BIG\"");
        machine.run_frames(60);
        assert!(
            machine.screen_text().contains("SAVED"),
            "the machine says the save happened:\n{}",
            machine.screen_text()
        );
        machine.save_disk_image(&path).expect("the volume written back");

        let mut again = boot(Model::Kestrel16);
        again.mount_disk_image(&path).expect("the saved volume");
        again.type_line_paced("LOAD \"BIG\"");
        again.type_line_paced("RUN");
        again.run_frames(80);

        let screen = again.screen_text();
        assert!(
            screen.lines().any(|row| row.trim() == "LINE 20"),
            "the middle of the program came back:\n{screen}"
        );
        assert!(
            screen.lines().any(|row| row.trim() == "LINE 40"),
            "and so did its last line:\n{screen}"
        );
        assert!(
            screen.lines().any(|row| row.trim() == "42"),
            "which ran, so the store is whole and not written twice:\n{screen}"
        );
        let _ = std::fs::remove_file(&path);
    }

    /// Put a call in RAM, point the processor at it and read what is left behind:
    /// how a KERNAL service is called by a program on this machine, and the only
    /// way to see what one returns without the language standing in between.
    fn call_kernal(machine: &mut Machine, code: &[u8]) -> (u8, u16, bool) {
        for (index, byte) in code.iter().enumerate() {
            machine.bus.write(0x0600 + index as u32, *byte);
        }
        match machine.cpu {
            Cpu::Mos65816(ref mut cpu) => {
                let mut registers = cpu.registers();
                registers.pc = 0x0600;
                registers.pbr = 0;
                registers.dbr = 0;
                registers.d = 0;
                registers.emulation = false;
                // eight-bit accumulator, sixteen-bit index registers: the state
                // the KERNAL's jump table expects a caller to be in, and the
                // state the language itself calls in (SEP #$20, X left wide)
                registers.p = 0x20;
                registers.s = 0x01F3;
                cpu.set_registers(&registers);
            }
            _ => panic!("the 1992 machine has a 65816"),
        }
        // Run until the call has come back and the processor is spinning at the
        // jump: the status is whatever the service left, and a fixed number of
        // frames would read the middle of an interrupt instead.
        for _ in 0..4_000_000 {
            match machine.cpu {
                Cpu::Mos65816(ref cpu) if cpu.registers().pc == 0x060A => break,
                _ => machine.run_cycles(1),
            }
        }
        match machine.cpu {
            Cpu::Mos65816(ref cpu) => {
                let registers = cpu.registers();
                (registers.c as u8, registers.x, registers.p & 0x01 != 0)
            }
            _ => panic!("the 1992 machine has a 65816"),
        }
    }

    /// `DOSFIND`, as the manual promises it: A = a sixteen-bit pointer to a
    /// sixteen-byte space-padded name, X = the directory entry, `$69` and `$6A`
    /// = its first sector, and carry set when the volume has no such name.
    #[test]
    fn the_1992_dos_finds_a_name_where_it_says_it_does() {
        let path = std::env::temp_dir().join("kestrel16-dos-find.dsk");
        std::fs::write(&path, vec![0u8; 4096 * 512]).expect("a blank volume");
        let mut machine = boot(Model::Kestrel16);
        machine.mount_disk_image(&path).expect("a disk in the drive");
        machine.type_line_paced("FORMAT");
        machine.type_line_paced("10 PRINT 1");
        machine.type_line_paced("SAVE \"TEST\"");
        machine.run_frames(40);

        // clear of the call below, which goes at $0600
        for (at, name) in [(0x0640u32, "NOPE"), (0x0650, "TEST")] {
            let mut padded = [32u8; 16];
            padded[..name.len()].copy_from_slice(name.as_bytes());
            for (index, byte) in padded.iter().enumerate() {
                machine.bus.write(at + index as u32, *byte);
            }
        }

        // REP #$20 / LDA.W #$0650 / SEP #$20 / JSR $E027 / JMP to itself
        let found = [0xC2, 0x20, 0xA9, 0x50, 0x06, 0xE2, 0x20, 0x20, 0x27, 0xE0, 0x4C, 0x0A, 0x06];
        let (status, entry, missing) = call_kernal(&mut machine, &found);
        let first = u16::from(machine.bus.read(0x69)) | (u16::from(machine.bus.read(0x6A)) << 8);
        assert_eq!(status, 0, "DOSFIND says it found the name");
        assert!(!missing, "and clears carry");
        assert_eq!(entry, 0, "TEST is the first entry");
        assert_eq!(first, 2, "and its data starts at the first free sector");

        // The same call with the name that is not on the volume.
        let absent = [0xC2, 0x20, 0xA9, 0x40, 0x06, 0xE2, 0x20, 0x20, 0x27, 0xE0, 0x4C, 0x0A, 0x06];
        let (status, _, missing) = call_kernal(&mut machine, &absent);
        assert_eq!(status, 1, "a name that is not there is an error");
        assert!(missing, "and the carry says so, which is the documented answer");
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
    /// Storing a line is not a command, so it does not announce itself: the
    /// prompt is what says a command has finished, and a numbered line only goes
    /// into the program. The power-on `READY.` is counted first, so only the
    /// prompts this typing earns are the ones under test.
    #[test]
    fn storing_a_line_does_not_echo_the_prompt() {
        for model in [Model::Kestrel, Model::Kestrel16] {
            let mut machine = boot(model);
            let prompts = |text: &str| text.matches("READY.").count();
            let at_boot = prompts(&machine.screen_text());

            machine.type_line_paced("10 REM A STORED LINE");
            let after_store = machine.screen_text();
            assert_eq!(
                prompts(&after_store),
                at_boot,
                "storing a line said READY. on {model:?}:\n{after_store}"
            );

            machine.type_line_paced("PRINT 7");
            let after_command = machine.screen_text();
            assert_eq!(
                prompts(&after_command),
                at_boot + 1,
                "a command says READY. once on {model:?}:\n{after_command}"
            );
        }
    }

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
