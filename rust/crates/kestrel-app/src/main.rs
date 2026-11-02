// SPDX-License-Identifier: MIT
//
// kestrel_computers -- the machines' window, in egui.
//
// One executable, two machines, because they are one emulator: `--model kestrel`
// powers on the 1982 machine, `--model kestrel16` the 1992 one.
//
// The window is shaped after WinUAE, which is the pattern a machine's front end
// has settled on: the tube in the main window, a menu bar above it and a status
// line below it, and one Settings window -- F12, as on the Amiga emulator -- with
// a tree of properties down the left and the chosen page, in groups, on the
// right. OK applies, Cancel puts everything back, Reset cold-boots.
//
// Everything on the tube is machine code in a ROM image; the window only decides
// how many cycles pass and what colour each attribute byte is.

use std::io::Write as _;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use eframe::egui::{
    self, Align, Color32, CornerRadius, FontId, Layout, Margin, Pos2, Rect, RichText, Stroke,
    StrokeKind, Vec2,
};
use kestrel_machine::machine::Machine;
use kestrel_machine::profile::{profile_of, Model};

/// The Kestrel's sixteen colours. Its attribute byte holds two of them: ink in
/// the high nibble, paper in the low.
const PALETTE_1982: [Color32; 16] = [
    Color32::from_rgb(0x00, 0x00, 0x00), // black
    Color32::from_rgb(0xFF, 0xFF, 0xFF), // white
    Color32::from_rgb(0x9F, 0x3B, 0x3B), // red
    Color32::from_rgb(0x6A, 0xB6, 0xC4), // cyan
    Color32::from_rgb(0x9C, 0x5A, 0xC4), // purple
    Color32::from_rgb(0x4E, 0x9E, 0x4E), // green
    Color32::from_rgb(0x3B, 0x4E, 0xB6), // blue
    Color32::from_rgb(0xC4, 0xC4, 0x4E), // yellow
    Color32::from_rgb(0xB6, 0x7A, 0x3B), // orange
    Color32::from_rgb(0x8B, 0x5A, 0x2B), // brown
    Color32::from_rgb(0xD9, 0x7A, 0x7A), // light red
    Color32::from_rgb(0x5A, 0x5A, 0x5A), // dark grey
    Color32::from_rgb(0x9E, 0x9E, 0x9E), // grey
    Color32::from_rgb(0x9E, 0xD9, 0x9E), // light green
    Color32::from_rgb(0x7A, 0x8B, 0xD9), // light blue
    Color32::from_rgb(0xC4, 0xC4, 0xC4), // light grey
];

/// The Kestrel 16's sixteen colours, which are a different set of codes: bit 0
/// blue, bit 1 red, bit 2 green, bit 3 brightness. Muted to suit the decade.
const PALETTE_1992: [Color32; 16] = [
    Color32::from_rgb(0x00, 0x00, 0x00), // black
    Color32::from_rgb(0x2E, 0x4F, 0xA8), // blue
    Color32::from_rgb(0xA8, 0x30, 0x38), // red
    Color32::from_rgb(0xA8, 0x40, 0x9C), // magenta
    Color32::from_rgb(0x2F, 0x9B, 0x52), // green
    Color32::from_rgb(0x2E, 0x9B, 0xA8), // cyan
    Color32::from_rgb(0xA8, 0x9A, 0x32), // yellow
    Color32::from_rgb(0xB0, 0xB0, 0xB0), // light grey
    Color32::from_rgb(0x4C, 0x4C, 0x4C), // dark grey
    Color32::from_rgb(0x5A, 0x74, 0xD8), // light blue
    Color32::from_rgb(0xD8, 0x6A, 0x6A), // light red
    Color32::from_rgb(0xD0, 0x78, 0xC0), // light magenta
    Color32::from_rgb(0x6F, 0xC8, 0x8A), // light green
    Color32::from_rgb(0x6F, 0xC8, 0xD0), // light cyan
    Color32::from_rgb(0xD8, 0xCC, 0x7A), // light yellow
    Color32::from_rgb(0xF0, 0xF0, 0xF0), // white
];

/// The colours the machine in the window draws with.
fn palette(model: Model) -> &'static [Color32; 16] {
    match model {
        Model::Kestrel => &PALETTE_1982,
        Model::Kestrel16 => &PALETTE_1992,
    }
}

// The window's own colours: a dark bench that stays out of the tube's way.
const BG: Color32 = Color32::from_rgb(0x0E, 0x10, 0x15);
const PANEL: Color32 = Color32::from_rgb(0x15, 0x18, 0x1F);
const WELL: Color32 = Color32::from_rgb(0x11, 0x14, 0x1A);
const RAISED: Color32 = Color32::from_rgb(0x1D, 0x22, 0x2B);
const HOVER: Color32 = Color32::from_rgb(0x26, 0x2D, 0x39);
const PRESSED: Color32 = Color32::from_rgb(0x2F, 0x38, 0x46);
const EDGE: Color32 = Color32::from_rgb(0x2A, 0x31, 0x3E);
const BEZEL: Color32 = Color32::from_rgb(0x05, 0x06, 0x09);
const TEXT: Color32 = Color32::from_rgb(0xD8, 0xDE, 0xE8);
const MUTED: Color32 = Color32::from_rgb(0x77, 0x82, 0x93);
const ACCENT: Color32 = Color32::from_rgb(0x5B, 0xCB, 0xB2);
const WARN: Color32 = Color32::from_rgb(0xE0, 0xA0, 0x5A);
const LAMP_OFF: Color32 = Color32::from_rgb(0x34, 0x3B, 0x48);

#[derive(Clone, Copy, PartialEq, Eq)]
enum Speed {
    Mhz1,
    Mhz2,
    Mhz4,
    Mhz8,
    Turbo,
}

impl Speed {
    const ALL: [Speed; 5] = [
        Speed::Mhz1,
        Speed::Mhz2,
        Speed::Mhz4,
        Speed::Mhz8,
        Speed::Turbo,
    ];

    fn label(self) -> &'static str {
        match self {
            Speed::Mhz1 => "1 MHz",
            Speed::Mhz2 => "2 MHz",
            Speed::Mhz4 => "4 MHz",
            Speed::Mhz8 => "8 MHz",
            Speed::Turbo => "Turbo",
        }
    }

    fn file_value(self) -> &'static str {
        match self {
            Speed::Mhz1 => "1",
            Speed::Mhz2 => "2",
            Speed::Mhz4 => "4",
            Speed::Mhz8 => "8",
            Speed::Turbo => "turbo",
        }
    }

    /// How many of the processor's cycles the host gives it per second.
    fn cycles_per_second(self) -> f64 {
        match self {
            Speed::Mhz1 => 1_000_000.0,
            Speed::Mhz2 => 2_000_000.0,
            Speed::Mhz4 => 4_000_000.0,
            Speed::Mhz8 => 8_000_000.0,
            Speed::Turbo => 0.0,
        }
    }

    /// The setting that is the machine's own clock.
    fn at_hertz(hertz: u32) -> Self {
        match hertz {
            1_000_000 => Speed::Mhz1,
            2_000_000 => Speed::Mhz2,
            4_000_000 => Speed::Mhz4,
            _ => Speed::Mhz8,
        }
    }
}

/// Everything the Settings window can change. It is small enough to copy, which
/// is what Cancel needs: the window holds a draft and the machine holds this.
#[derive(Clone, Copy, PartialEq)]
struct Settings {
    model: Model,
    speed: Speed,
    zoom: f32,
    show_border: bool,
    show_cursor: bool,
    sound: bool,
}

impl Settings {
    fn for_model(model: Model) -> Self {
        Self {
            model,
            speed: Speed::at_hertz(profile_of(model).clock_hz),
            zoom: 100.0,
            show_border: true,
            show_cursor: true,
            sound: true,
        }
    }

    /// The switches as a file holds them, one `name=value` to a line.
    fn text(self) -> String {
        let machine = match self.model {
            Model::Kestrel => "kestrel",
            Model::Kestrel16 => "kestrel16",
        };
        format!(
            "# Kestrel Computers settings\n\
             machine={machine}\n\
             speed={}\n\
             zoom={:.0}\n\
             border={}\n\
             cursor={}\n\
             sound={}\n",
            self.speed.file_value(),
            self.zoom,
            self.show_border as u8,
            self.show_cursor as u8,
            self.sound as u8,
        )
    }

    fn parse(text: &str) -> Result<Self, String> {
        let mut settings = Settings::for_model(Model::Kestrel);
        for (index, line) in text.lines().enumerate() {
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let Some((key, value)) = line.split_once('=') else {
                return Err(format!("line {}: no = in {line:?}", index + 1));
            };
            let line_number = index + 1;
            match key.trim() {
                "machine" => {
                    settings.model = match value.trim() {
                        "kestrel" => Model::Kestrel,
                        "kestrel16" => Model::Kestrel16,
                        other => return Err(format!("line {line_number}: no such machine: {other}")),
                    }
                }
                "speed" => {
                    settings.speed = match value.trim() {
                        "1" => Speed::Mhz1,
                        "2" => Speed::Mhz2,
                        "4" => Speed::Mhz4,
                        "8" => Speed::Mhz8,
                        "turbo" => Speed::Turbo,
                        other => return Err(format!("line {line_number}: no such speed: {other}")),
                    }
                }
                "zoom" => {
                    settings.zoom = value
                        .trim()
                        .parse::<f32>()
                        .map_err(|_| format!("line {line_number}: not a zoom: {}", value.trim()))?
                        .clamp(50.0, 300.0);
                }
                "border" => settings.show_border = truthy(value, line_number)?,
                "cursor" => settings.show_cursor = truthy(value, line_number)?,
                "sound" => settings.sound = truthy(value, line_number)?,
                _ => {} // a setting this window does not know: leave it alone
            }
        }
        Ok(settings)
    }
}

fn truthy(value: &str, line: usize) -> Result<bool, String> {
    match value.trim() {
        "1" | "true" | "yes" | "on" => Ok(true),
        "0" | "false" | "no" | "off" => Ok(false),
        other => Err(format!("line {line}: not a yes or no: {other}")),
    }
}

/// A page of the Settings window. The first field of each row in `PAGES` is the
/// heading it sits under in the tree.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Page {
    Quickstart,
    Machine,
    Speed,
    Rom,
    Cartridge,
    Disk,
    Tape,
    Display,
    Sound,
    About,
}

const PAGES: [(Page, &str, &str); 10] = [
    (Page::Quickstart, "Quickstart", "Machine"),
    (Page::Machine, "Machine", "Machine"),
    (Page::Speed, "CPU & speed", "Machine"),
    (Page::Rom, "ROM", "Machine"),
    (Page::Cartridge, "Cartridge", "Hardware"),
    (Page::Disk, "Floppy disk", "Hardware"),
    (Page::Tape, "Tape", "Hardware"),
    (Page::Display, "Display", "Host"),
    (Page::Sound, "Sound", "Host"),
    (Page::About, "About", "Help"),
];

impl Page {
    fn title(self) -> &'static str {
        PAGES
            .iter()
            .find(|(page, _, _)| *page == self)
            .map(|(_, title, _)| *title)
            .unwrap_or("Quickstart")
    }
}

/// A file the window is about to read or write.
#[derive(Clone, Copy, PartialEq, Eq)]
enum FileKind {
    Disk,
    SaveDisk,
    Tape,
    SaveTape,
    Cartridge,
    Roms,
    Screenshot,
    SaveConfig,
    LoadConfig,
}

impl FileKind {
    fn title(self) -> &'static str {
        match self {
            FileKind::Disk => "Mount a disk image",
            FileKind::SaveDisk => "Write the disk image",
            FileKind::Tape => "Insert a tape",
            FileKind::SaveTape => "Write the tape",
            FileKind::Cartridge => "Fit a cartridge",
            FileKind::Roms => "Choose a ROM directory",
            FileKind::Screenshot => "Save a screenshot",
            FileKind::SaveConfig => "Save a configuration",
            FileKind::LoadConfig => "Load a configuration",
        }
    }

    fn verb(self) -> &'static str {
        match self {
            FileKind::SaveDisk
            | FileKind::SaveTape
            | FileKind::Screenshot
            | FileKind::SaveConfig => "Save",
            _ => "Open",
        }
    }

    fn default_name(self) -> &'static str {
        match self {
            FileKind::Disk | FileKind::SaveDisk => "disk.dsk",
            FileKind::Tape | FileKind::SaveTape => "tape.tap",
            FileKind::Cartridge => "cartridge.bin",
            FileKind::Screenshot => "kestrel.png",
            FileKind::Roms => "build/generated",
            FileKind::SaveConfig | FileKind::LoadConfig => "kestrel.conf",
        }
    }
}

/// The little dialog a menu opens: one path, remembered from last time.
struct Prompt {
    kind: FileKind,
    title: String,
    hint: String,
    path: String,
    focus: bool,
}

struct App {
    machine: Machine,
    /// Images to load from disk, when `--roms` said so; otherwise the ROMs are
    /// assembled from their sources.
    roms: Option<PathBuf>,
    settings: Settings,
    /// The Settings window's working copy. `Some` means the window is open, and
    /// the machine stands still while its switches are out.
    draft: Option<Settings>,
    page: Page,
    last: Instant,
    /// What the window last said, so a message can be read without leaving.
    message: String,
    /// The files the media came from, so what the machine writes can go back
    /// where it came from.
    disk_path: Option<PathBuf>,
    tape_path: Option<PathBuf>,
    cartridge_path: Option<PathBuf>,
    shipped_cartridge: String,
    /// A PNG to write and then leave, which is what a headless run wants: the
    /// machine photographs itself once the banner is complete.
    shot: Option<PathBuf>,
    frames_run: u32,
    running: bool,
    help: bool,
    prompt: Option<Prompt>,
    last_dir: PathBuf,
    notes_seen: u32,
    bell_until: Option<Instant>,
}

impl App {
    fn new(model: Model, roms: Option<PathBuf>) -> Result<Self, String> {
        let machine = boot(model, roms.clone())?;
        Ok(Self {
            settings: Settings::for_model(model),
            machine,
            roms,
            draft: None,
            page: Page::Quickstart,
            last: Instant::now(),
            message: String::new(),
            disk_path: None,
            tape_path: None,
            cartridge_path: None,
            shipped_cartridge: String::new(),
            shot: None,
            frames_run: 0,
            running: true,
            help: false,
            prompt: None,
            last_dir: std::env::current_dir().unwrap_or_else(|_| PathBuf::from(".")),
            notes_seen: 0,
            bell_until: None,
        })
    }

    fn set_message(&mut self, text: impl Into<String>) {
        self.message = text.into();
    }

    /// A cartridge in the slot. The reset is what fits one: the KERNAL looks at
    /// the slot once, on the way up, so fitting one means starting the machine
    /// again, which is also what a person did with the power switch.
    fn fit_cartridge(&mut self, bytes: Vec<u8>, name: String, path: Option<PathBuf>) {
        self.machine.bus.cartridge = Some(bytes);
        self.cartridge_path = path;
        self.machine.reset();
        self.set_message(format!("{name} in the cartridge slot"));
    }

    /// The `--cart` argument: a name the repository ships, or an image.
    fn fit_cartridge_argument(&mut self, which: &str) {
        let path = PathBuf::from(which);
        if path.is_file() {
            self.fit_cartridge_file(&path);
            return;
        }
        match kestrel_machine::roms::cartridge(self.machine.model(), which) {
            Ok(bytes) => self.fit_cartridge(bytes, which.to_string(), None),
            Err(error) => self.set_message(format!("cartridge {which}: {error}")),
        }
    }

    /// A cartridge image from the host, checked against the size of the slot.
    fn fit_cartridge_file(&mut self, path: &Path) {
        let name = path
            .file_name()
            .map(|name| name.to_string_lossy().into_owned())
            .unwrap_or_else(|| path.display().to_string());
        match std::fs::read(path) {
            Ok(bytes) => {
                let wanted = self.machine.bus.profile.cartridge_size as usize;
                if bytes.len() == wanted {
                    self.remember(path);
                    self.fit_cartridge(bytes, name, Some(path.to_path_buf()));
                } else {
                    self.set_message(format!(
                        "{name}: {} bytes, not the {wanted} a cartridge is",
                        bytes.len()
                    ));
                }
            }
            Err(error) => self.set_message(format!("{}: {error}", path.display())),
        }
    }

    fn settings_open(&self) -> bool {
        self.draft.is_some()
    }

    fn open_settings(&mut self) {
        self.draft = Some(self.settings);
        self.last = Instant::now();
    }

    /// Power a machine on. A different model is a different machine, so this is a
    /// cold boot and anything in memory is lost; the same model is a reset.
    fn power_on(&mut self, model: Model, ctx: &egui::Context) {
        if model == self.machine.model() {
            self.machine.reset();
            self.notes_seen = 0;
            self.frames_run = 0;
            self.set_message("cold booted");
            return;
        }
        match boot(model, self.roms.clone()) {
            Ok(machine) => {
                self.machine = machine;
                self.settings.model = model;
                self.settings.speed = Speed::at_hertz(self.machine.bus.profile.clock_hz);
                self.disk_path = None;
                self.tape_path = None;
                self.cartridge_path = None;
                self.frames_run = 0;
                self.notes_seen = 0;
                self.set_message("powered on");
                ctx.send_viewport_cmd(egui::ViewportCommand::InnerSize(default_window(model)));
                ctx.send_viewport_cmd(egui::ViewportCommand::Title(window_title(model)));
            }
            Err(message) => self.set_message(message),
        }
    }

    /// Apply a whole set of switches at once: this is what OK and a loaded
    /// configuration both do.
    fn apply_settings(&mut self, settings: Settings, ctx: &egui::Context) {
        if settings.model != self.machine.model() {
            self.power_on(settings.model, ctx);
        }
        self.settings = settings;
        self.last = Instant::now();
    }

    /// Assemble (or mount) the ROMs again, which is what changing the source of
    /// the images means.
    fn reload_roms(&mut self) {
        match boot(self.machine.model(), self.roms.clone()) {
            Ok(machine) => {
                self.machine = machine;
                self.notes_seen = 0;
                self.set_message("the ROMs were built again");
            }
            Err(message) => self.set_message(message),
        }
    }

    /// A language in the machine: one of the ROMs `roms/` holds, assembled and
    /// put in the socket (or, on the Kestrel 16, in its language bank).
    fn mount_socket_rom(&mut self, id: &str) {
        let model = self.machine.model();
        match self.machine.mount_assembled_language(id) {
            Ok(()) => {
                self.machine.reset();
                self.notes_seen = 0;
                let title = kestrel_machine::roms::language_title(model, id);
                let where_it_is = match model {
                    Model::Kestrel => "socket",
                    Model::Kestrel16 => "language bank",
                };
                self.set_message(format!("{title} in the {where_it_is}"));
            }
            Err(error) => self.set_message(error.to_string()),
        }
    }

    /// A file dropped on the window goes where its size says it belongs: a
    /// cartridge is the profile's cartridge size, a socket ROM the language's, a
    /// disk is the whole volume, and anything else is a tape.
    fn accept_dropped(&mut self, bytes: &[u8], name: &str, path: Option<PathBuf>) {
        let profile = self.machine.bus.profile;
        if bytes.len() == self.machine.bus.disk.image_size() {
            if self.machine.bus.disk.load(bytes) {
                if let Some(path) = path.clone() {
                    self.remember(&path);
                }
                self.disk_path = path;
                self.set_message(format!("{name} in the drive"));
            }
            return;
        }
        if bytes.len() == profile.cartridge_size as usize {
            if let Some(path) = path.clone() {
                self.remember(&path);
            }
            self.fit_cartridge(bytes.to_vec(), name.to_string(), path);
        } else if bytes.len() == profile.basic_size as usize && self.machine.model() == Model::Kestrel
        {
            match self.machine.mount_basic(bytes) {
                Ok(()) => {
                    self.machine.reset();
                    self.set_message(format!("{name} in the socket"));
                }
                Err(error) => self.set_message(error.to_string()),
            }
        } else {
            self.machine.bus.tape.load(bytes);
            self.machine.bus.tape.insert();
            if let Some(path) = path.clone() {
                self.remember(&path);
            }
            self.tape_path = path;
            self.set_message(format!("{name} on the tape"));
        }
    }

    fn remember(&mut self, path: &Path) {
        if let Some(parent) = path.parent() {
            if !parent.as_os_str().is_empty() {
                self.last_dir = parent.to_path_buf();
            }
        }
    }

    /// The keys a person pressed, as the machine's controller sees them. The
    /// window has no keyboard of its own: it reports and the ROM decides.
    fn take_keys(&mut self, ctx: &egui::Context) {
        let events = ctx.input(|input| input.events.clone());
        for event in events {
            match event {
                egui::Event::Text(text) => {
                    for character in text.chars() {
                        let code = character as u32;
                        if (32..127).contains(&code) {
                            self.machine.type_key(code as u8);
                        }
                    }
                }
                egui::Event::Key {
                    key,
                    pressed: true,
                    modifiers,
                    ..
                } => {
                    if modifiers.command {
                        match key {
                            egui::Key::Q => ctx.send_viewport_cmd(egui::ViewportCommand::Close),
                            egui::Key::R => {
                                self.machine.reset();
                                self.notes_seen = 0;
                                self.set_message("reset");
                            }
                            egui::Key::P => self.running = !self.running,
                            egui::Key::Comma => self.open_settings(),
                            _ => {}
                        }
                        continue;
                    }
                    let code = match key {
                        egui::Key::Enter => Some(13),
                        egui::Key::Backspace => Some(8),
                        egui::Key::Escape => Some(27),
                        egui::Key::Delete => Some(127),
                        egui::Key::ArrowLeft => Some(28),
                        egui::Key::ArrowRight => Some(29),
                        egui::Key::ArrowUp => Some(30),
                        egui::Key::ArrowDown => Some(31),
                        _ => None,
                    };
                    if let Some(code) = code {
                        self.machine.type_key(code);
                    }
                    if key == egui::Key::Pause || (key == egui::Key::F1 && modifiers.shift) {
                        self.machine.press_break();
                        self.set_message("BREAK");
                    }
                }
                _ => {}
            }
        }
    }

    /// Run the machine for the time that has passed. Turbo means "as much as this
    /// computer can do", which is a fixed slice of frames per update.
    fn tick(&mut self) {
        let now = Instant::now();
        let elapsed = now.duration_since(self.last);
        self.last = now;
        if !self.running || self.settings_open() {
            return;
        }
        let before = self.machine.bus.frame_counter;
        match self.settings.speed {
            Speed::Turbo => {
                let slice = self.machine.bus.profile.cycles_per_frame as i64 * 2;
                self.machine.run_cycles(slice);
            }
            _ => {
                let cycles = elapsed.as_secs_f64() * self.settings.speed.cycles_per_second();
                if cycles >= 1.0 {
                    self.machine.run_cycles(cycles as i64);
                }
            }
        }
        self.frames_run = self
            .frames_run
            .saturating_add(u32::from(self.machine.bus.frame_counter.wrapping_sub(before)));
        self.ring_bell();
    }

    /// The sound latch is a note. The host answers with its own bell and a lamp,
    /// because a machine this small has one wire for sound and no waveform.
    fn ring_bell(&mut self) {
        let notes = self.machine.bus.notes_played;
        if notes == self.notes_seen {
            return;
        }
        self.notes_seen = notes;
        self.bell_until = Some(Instant::now() + Duration::from_millis(220));
        if self.settings.sound {
            let mut out = std::io::stdout();
            let _ = out.write_all(b"\x07");
            let _ = out.flush();
        }
    }

    // -----------------------------------------------------------------------
    // The menu bar and the status line
    // -----------------------------------------------------------------------

    fn menus(&mut self, ui: &mut egui::Ui) {
        egui::Panel::top("menus")
            .frame(
                egui::Frame::new()
                    .fill(PANEL)
                    .inner_margin(Margin::symmetric(10, 4)),
            )
            .show(ui, |ui| {
                egui::MenuBar::new().ui(ui, |ui| {
                    ui.menu_button("Machine", |ui| {
                        if ui.button("Reset").clicked() {
                            self.machine.reset();
                            self.notes_seen = 0;
                            self.set_message("reset");
                        }
                        if ui.button("BREAK").clicked() {
                            self.machine.press_break();
                            self.set_message("BREAK");
                        }
                        let pause = if self.running { "Pause" } else { "Resume" };
                        if ui.button(pause).clicked() {
                            self.running = !self.running;
                        }
                        ui.separator();
                        if ui.button("Reload the ROMs").clicked() {
                            self.reload_roms();
                        }
                    });
                    ui.menu_button("Media", |ui| {
                        ui.menu_button("Floppy disk", |ui| self.disk_menu(ui));
                        ui.menu_button("Tape", |ui| self.tape_menu(ui));
                        ui.menu_button("Cartridge", |ui| self.cartridge_menu(ui));
                    });
                    ui.menu_button("Settings", |ui| {
                        if ui.button("Settings\u{2026}").clicked() {
                            self.open_settings();
                        }
                        ui.separator();
                        if ui.button("Save configuration\u{2026}").clicked() {
                            self.open_prompt(FileKind::SaveConfig);
                        }
                        if ui.button("Load configuration\u{2026}").clicked() {
                            self.open_prompt(FileKind::LoadConfig);
                        }
                    });
                    ui.menu_button("View", |ui| {
                        let mut zoom = self.settings.zoom;
                        ui.add(
                            egui::Slider::new(&mut zoom, 50.0..=300.0)
                                .suffix("%")
                                .text("Zoom")
                                .fixed_decimals(0),
                        );
                        self.settings.zoom = zoom;
                        if ui.button("Fit the window").clicked() {
                            self.settings.zoom = 100.0;
                        }
                        ui.separator();
                        ui.checkbox(&mut self.settings.show_border, "Show the border colour");
                        ui.checkbox(&mut self.settings.show_cursor, "Show the cursor");
                        ui.separator();
                        if ui.button("Screenshot\u{2026}").clicked() {
                            self.open_prompt(FileKind::Screenshot);
                        }
                    });
                    ui.menu_button("Help", |ui| {
                        if ui.button("Commands").clicked() {
                            self.help = true;
                        }
                        ui.separator();
                        ui.label(RichText::new("Kestrel Computers").strong().color(TEXT));
                        ui.label(
                            RichText::new("Two machines, one window, built in Rust")
                                .small()
                                .color(MUTED),
                        );
                    });
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        if self.settings_open() {
                            badge(ui, "SETTINGS", WARN);
                        } else if !self.running {
                            badge(ui, "PAUSED", WARN);
                        }
                        badge(ui, machine_name(self.machine.model()), ACCENT);
                    });
                });
            });
    }

    fn status_bar(&mut self, ui: &mut egui::Ui) {
        let profile = self.machine.bus.profile;
        ui.horizontal(|ui| {
            // The clock, and the speed only when it is not the machine's own:
            // two bare numbers for the same thing read as a mistake.
            let clock = profile.clock_hz / 1_000_000;
            let own = Speed::at_hertz(profile.clock_hz);
            let clock_text = if self.settings.speed == own {
                format!("{clock} MHz")
            } else {
                format!("{clock} MHz ({})", self.settings.speed.label())
            };
            ui.label(RichText::new(clock_text).small().color(TEXT));
            dot(ui);
            lamp(
                ui,
                "DISK",
                self.machine.bus.disk.present(),
                &format!(
                    "{} \u{2014} {}",
                    self.machine.bus.disk.volume_name(),
                    size_text(self.machine.bus.disk.image_size())
                ),
            );
            lamp(
                ui,
                "TAPE",
                self.machine.bus.tape.status() & 0x80 != 0,
                &format!("{} bytes on the tape", self.machine.bus.tape.image().len()),
            );
            lamp(
                ui,
                "CART",
                self.machine.bus.cartridge.is_some(),
                "an expansion cartridge",
            );
            if self.bell_until.is_some_and(|until| Instant::now() < until) {
                ui.label(RichText::new("\u{266A}").color(ACCENT));
            }

            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                ui.label(
                    RichText::new("F12 settings \u{2022} Ctrl+Q quits \u{2022} drop a disk here")
                        .small()
                        .color(MUTED),
                );
                if !self.message.is_empty() {
                    ui.separator();
                    ui.label(RichText::new(&self.message).small().color(ACCENT));
                }
            });
        });
    }

    // -----------------------------------------------------------------------
    // The Settings window
    // -----------------------------------------------------------------------

    /// The settings, shaped after WinUAE's: a tree of properties down the left,
    /// the chosen page in groups on the right, and OK, Cancel and Reset along
    /// the foot. The sheet is modal in the sense that matters -- the machine
    /// stands still while its switches are out.
    fn settings_window(&mut self, ctx: &egui::Context) {
        let Some(mut draft) = self.draft.take() else {
            return;
        };
        let mut open = true;
        let mut commit = false;
        let mut cancel = false;

        egui::Window::new("Settings")
            .open(&mut open)
            .collapsible(false)
            .resizable(true)
            .default_pos(Pos2::new(10.0, 38.0))
            .default_size(Vec2::new(600.0, 520.0))
            .min_size(Vec2::new(560.0, 440.0))
            .show(ctx, |ui| {
                ui.allocate_ui_with_layout(
                    Vec2::new(560.0, 400.0),
                    Layout::left_to_right(Align::TOP),
                    |ui| {
                    ui.vertical(|ui| {
                        ui.set_width(132.0);
                        ui.label(RichText::new("PROPERTIES").small().strong().color(MUTED));
                        ui.add_space(6.0);
                        let mut heading = "";
                        for (page, title, group) in PAGES {
                            if group != heading {
                                heading = group;
                                ui.add_space(8.0);
                                ui.label(RichText::new(group.to_uppercase()).small().color(EDGE));
                            }
                            if ui.selectable_label(self.page == page, title).clicked() {
                                self.page = page;
                            }
                        }
                    });
                    ui.separator();
                    ui.vertical(|ui| {
                        ui.set_min_width(400.0);
                        ui.set_max_width(430.0);
                        ui.label(
                            RichText::new(self.page.title())
                                .strong()
                                .size(17.0)
                                .color(TEXT),
                        );
                        ui.add_space(6.0);
                        egui::ScrollArea::vertical()
                            .id_salt("settings-page")
                            .auto_shrink([false, true])
                            .max_height(360.0)
                            .show(ui, |ui| {
                                match self.page {
                                    Page::Quickstart => self.page_quickstart(ui, &mut draft),
                                    Page::Machine => self.page_machine(ui, &draft),
                                    Page::Speed => self.page_speed(ui, &mut draft),
                                    Page::Rom => self.page_rom(ui),
                                    Page::Cartridge => self.page_cartridge(ui),
                                    Page::Disk => self.page_disk(ui),
                                    Page::Tape => self.page_tape(ui),
                                    Page::Display => self.page_display(ui, &mut draft),
                                    Page::Sound => self.page_sound(ui, &mut draft),
                                    Page::About => self.page_about(ui),
                                }
                            });
                    });
                    },
                );
                ui.separator();
                ui.horizontal(|ui| {
                    let ok = egui::Button::new(
                        RichText::new("OK").strong().color(Color32::from_rgb(0x05, 0x1A, 0x15)),
                    )
                    .fill(ACCENT)
                    .corner_radius(CornerRadius::same(6))
                    .min_size(Vec2::new(58.0, 24.0));
                    if ui.add(ok).clicked() {
                        commit = true;
                    }
                    if ui.button("Cancel").clicked() {
                        cancel = true;
                    }
                    ui.separator();
                    if ui.button("Reset").clicked() {
                        self.machine.reset();
                        self.notes_seen = 0;
                        self.set_message("reset");
                    }
                    if ui.button("Commands\u{2026}").clicked() {
                        self.help = true;
                    }
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        ui.label(
                            RichText::new(if self.running {
                                "the machine runs behind this sheet"
                            } else {
                                "the machine is paused"
                            })
                            .small()
                            .color(MUTED),
                        );
                    });
                });
            });

        if commit {
            self.apply_settings(draft, ctx);
            self.draft = None;
        } else if cancel || !open {
            self.draft = None;
            self.set_message("settings put back");
        } else {
            self.draft = Some(draft);
        }
    }

    fn page_quickstart(&mut self, ui: &mut egui::Ui, draft: &mut Settings) {
        ui.label(
            RichText::new(
                "Two machines share one window. Each comes up at its own clock, with its own ROMs.",
            )
            .small()
            .color(MUTED),
        );
        ui.add_space(10.0);
        if machine_card(
            ui,
            draft.model == Model::Kestrel,
            "Kestrel",
            "1982 \u{2022} 6502 \u{2022} 1 MHz \u{2022} 40 \u{00D7} 25 \u{2022} 32 KiB",
        ) {
            draft.model = Model::Kestrel;
            draft.speed = Speed::at_hertz(profile_of(Model::Kestrel).clock_hz);
        }
        ui.add_space(6.0);
        if machine_card(
            ui,
            draft.model == Model::Kestrel16,
            "Kestrel 16",
            "1992 \u{2022} 65816 \u{2022} 8 MHz \u{2022} 80 \u{00D7} 50 \u{2022} 1 MiB",
        ) {
            draft.model = Model::Kestrel16;
            draft.speed = Speed::at_hertz(profile_of(Model::Kestrel16).clock_hz);
        }
        ui.add_space(12.0);
        group(ui, "This machine now", |ui| {
            ui.horizontal(|ui| {
                if ui.button("Reset").clicked() {
                    self.machine.reset();
                    self.notes_seen = 0;
                    self.set_message("reset");
                }
                if ui.button("BREAK").clicked() {
                    self.machine.press_break();
                    self.set_message("BREAK");
                }
                let pause = if self.running { "Pause" } else { "Resume" };
                if ui.button(pause).clicked() {
                    self.running = !self.running;
                }
            });
            ui.add_space(4.0);
            ui.label(
                RichText::new(
                    "Reset and BREAK act at once. A machine chosen above is powered on when you press OK.",
                )
                .small()
                .color(MUTED),
            );
        });
    }

    fn page_machine(&mut self, ui: &mut egui::Ui, draft: &Settings) {
        let profile = profile_of(draft.model);
        group(ui, "The machine", |ui| {
            ui.label(
                RichText::new(machine_name(draft.model))
                    .strong()
                    .size(15.0)
                    .color(TEXT),
            );
            ui.add_space(6.0);
            field(ui, "Processor", cpu_name(draft.model));
            field(ui, "Clock", &format!("{} MHz", profile.clock_hz / 1_000_000));
            field(ui, "Memory", &format!("{} KiB", profile.ram_bytes / 1024));
            field(
                ui,
                "Tube",
                &format!("{} \u{00D7} {} characters", profile.columns, profile.rows),
            );
            field(ui, "Palette", "16 colours, ink and paper per cell");
            field(
                ui,
                "Language ROM",
                &format!("{} KiB", profile.basic_size / 1024),
            );
            field(ui, "KERNAL", &format!("{} KiB", profile.kernal_size / 1024));
        });

        // The socket, or the Kestrel 16's language bank: whichever the machine
        // has, it is one image at a time, and the list is whatever `roms/` holds.
        let languages = kestrel_machine::roms::shipped_languages(draft.model).unwrap_or_default();
        let (heading, note) = match draft.model {
            Model::Kestrel => (
                "ROM socket",
                "Which 16 KiB image sits in the language socket. Changing it cold-boots the machine.",
            ),
            Model::Kestrel16 => (
                "Language bank",
                "Which 64 KiB image fills the language bank. Changing it cold-boots the machine.",
            ),
        };
        group(ui, heading, |ui| {
            ui.label(RichText::new(note).small().color(MUTED));
            ui.add_space(6.0);
            ui.horizontal_wrapped(|ui| {
                for name in &languages {
                    let title = kestrel_machine::roms::language_title(draft.model, name);
                    if ui.button(title).clicked() {
                        self.mount_socket_rom(name);
                    }
                }
            });
        });

        if draft.model != self.machine.model() {
            ui.label(
                RichText::new("OK powers this machine on; anything in memory is lost.")
                    .small()
                    .color(WARN),
            );
        }
    }

    fn page_speed(&mut self, ui: &mut egui::Ui, draft: &mut Settings) {
        let profile = profile_of(draft.model);
        group(ui, "Clock", |ui| {
            for speed in Speed::ALL {
                ui.radio_value(&mut draft.speed, speed, speed.label());
            }
        });
        group(ui, "The machine's own clock", |ui| {
            let own = Speed::at_hertz(profile.clock_hz);
            ui.label(
                RichText::new(format!(
                    "This machine is built for {}. Any other setting is a host convenience: the processor is the same one, and a program that depends on timing only works at its own clock.",
                    own.label()
                ))
                .small()
                .color(MUTED),
            );
            ui.add_space(6.0);
            if ui
                .button(format!("Use the machine's clock ({})", own.label()))
                .clicked()
            {
                draft.speed = own;
            }
        });
    }

    fn page_rom(&mut self, ui: &mut egui::Ui) {
        group(ui, "Where the images come from", |ui| {
            match &self.roms {
                Some(path) => field(ui, "Directory", &path.display().to_string()),
                None => field(ui, "Source", "assembled from roms/*.s at start-up"),
            }
            ui.add_space(6.0);
            ui.label(
                RichText::new(
                    "A directory must hold images built for this machine: the KERNAL, the language, and the Kestrel's monitor.",
                )
                .small()
                .color(MUTED),
            );
        });
        group(ui, "Actions", |ui| {
            ui.horizontal(|ui| {
                if ui.button("Choose a directory\u{2026}").clicked() {
                    self.open_prompt(FileKind::Roms);
                }
                if ui.button("Build them again").clicked() {
                    self.reload_roms();
                }
            });
        });
    }

    fn page_cartridge(&mut self, ui: &mut egui::Ui) {
        let profile = self.machine.bus.profile;
        group(ui, "Expansion cartridge", |ui| {
            field(
                ui,
                "State",
                if self.machine.bus.cartridge.is_some() {
                    "fitted"
                } else {
                    "empty"
                },
            );
            field(ui, "Size", &format!("{} KiB", profile.cartridge_size / 1024));
            field(
                ui,
                "Where",
                &format!("${:06X}", profile.cartridge_base),
            );
            match &self.cartridge_path {
                Some(path) => field(ui, "Image", &path.display().to_string()),
                None => field(ui, "Image", "not from a file"),
            }
            ui.add_space(6.0);
            ui.horizontal(|ui| {
                if ui.button("Fit\u{2026}").clicked() {
                    self.open_prompt(FileKind::Cartridge);
                }
                if ui.button("Eject").clicked() {
                    self.machine.bus.cartridge = None;
                    self.cartridge_path = None;
                    self.machine.reset();
                    self.set_message("cartridge out");
                }
            });
        });
        group(ui, "Cartridges this machine was sold", |ui| {
            let names = kestrel_machine::roms::shipped_cartridges(self.machine.model())
                .unwrap_or_default();
            if names.is_empty() {
                field(ui, "Catalogue", "none in cartridges/");
                return;
            }
            if self.shipped_cartridge.is_empty() {
                self.shipped_cartridge = names[0].clone();
            }
            ui.horizontal(|ui| {
                egui::ComboBox::from_id_salt("shipped-cartridge")
                    .selected_text(self.shipped_cartridge.clone())
                    .show_ui(ui, |ui| {
                        for name in &names {
                            ui.selectable_value(
                                &mut self.shipped_cartridge,
                                name.clone(),
                                name,
                            );
                        }
                    });
                if ui.button("Fit").clicked() {
                    let name = self.shipped_cartridge.clone();
                    match kestrel_machine::roms::cartridge(self.machine.model(), &name) {
                        Ok(bytes) => self.fit_cartridge(bytes, name, None),
                        Err(error) => self.set_message(format!("{name}: {error}")),
                    }
                }
            });
            field(ui, "Sources", "cartridges/");
        });
    }

    fn page_disk(&mut self, ui: &mut egui::Ui) {
        let disk = &self.machine.bus.disk;
        group(ui, "Volume", |ui| {
            field(ui, "Name", &disk.volume_name());
            field(ui, "Sectors", &format!("{}", disk.sectors()));
            field(ui, "Sector size", &format!("{} bytes", disk.sector_size()));
            field(ui, "Image", &size_text(disk.image_size()));
            field(
                ui,
                "State",
                if disk.present() {
                    "in the drive"
                } else {
                    "no disk"
                },
            );
            match &self.disk_path {
                Some(path) => field(ui, "File", &path.display().to_string()),
                None => field(ui, "File", "not from a file"),
            }
        });
        group(ui, "Actions", |ui| {
            ui.horizontal(|ui| {
                if ui.button("Mount\u{2026}").clicked() {
                    self.open_prompt(FileKind::Disk);
                }
                if ui.button("Save\u{2026}").clicked() {
                    self.open_prompt(FileKind::SaveDisk);
                }
                if ui.button("Format").clicked() {
                    self.machine.bus.disk.format("KESWARP");
                    self.set_message("a blank volume, named KESWARP");
                }
                if ui.button("Eject").clicked() {
                    self.machine.bus.disk.eject();
                    self.disk_path = None;
                    self.set_message("disk out");
                }
            });
        });
    }

    fn page_tape(&mut self, ui: &mut egui::Ui) {
        let status = self.machine.bus.tape.status();
        group(ui, "Cassette", |ui| {
            field(
                ui,
                "State",
                if status & 0x20 != 0 {
                    "recording"
                } else if status & 0x40 != 0 {
                    "playing"
                } else if status & 0x80 != 0 {
                    "inserted"
                } else {
                    "no tape"
                },
            );
            field(
                ui,
                "Length",
                &format!("{} bytes", self.machine.bus.tape.image().len()),
            );
            match &self.tape_path {
                Some(path) => field(ui, "File", &path.display().to_string()),
                None => field(ui, "File", "not from a file"),
            }
        });
        group(ui, "Actions", |ui| {
            ui.horizontal(|ui| {
                if ui.button("Insert\u{2026}").clicked() {
                    self.open_prompt(FileKind::Tape);
                }
                if ui.button("Save\u{2026}").clicked() {
                    self.open_prompt(FileKind::SaveTape);
                }
                if ui.button("Rewind").clicked() {
                    self.machine.bus.tape.rewind();
                    self.set_message("tape rewound");
                }
                if ui.button("Eject").clicked() {
                    self.machine.bus.tape = kestrel_machine::devices::Tape::new();
                    self.tape_path = None;
                    self.set_message("tape out");
                }
            });
        });
    }

    fn page_display(&mut self, ui: &mut egui::Ui, draft: &mut Settings) {
        group(ui, "Tube", |ui| {
            ui.add(
                egui::Slider::new(&mut draft.zoom, 50.0..=300.0)
                    .suffix("%")
                    .text("Zoom")
                    .fixed_decimals(0),
            );
            ui.add_space(4.0);
            ui.checkbox(&mut draft.show_border, "Show the border colour");
            ui.checkbox(&mut draft.show_cursor, "Show the cursor");
            ui.add_space(4.0);
            ui.label(
                RichText::new(
                    "100% is the largest tube that fits the window. Above it the window scrolls.",
                )
                .small()
                .color(MUTED),
            );
            if ui.button("Fit the window").clicked() {
                draft.zoom = 100.0;
            }
        });
        group(ui, "Palette", |ui| {
            ui.horizontal(|ui| {
                for (index, colour) in palette(self.machine.model()).iter().enumerate() {
                    swatch(ui, *colour, index);
                }
            });
            ui.add_space(4.0);
            ui.label(
                RichText::new(
                    "This machine's sixteen colour codes, one ink and one paper to a cell.",
                )
                .small()
                .color(MUTED),
            );
        });
    }

    fn page_sound(&mut self, ui: &mut egui::Ui, draft: &mut Settings) {
        group(ui, "Sound", |ui| {
            ui.checkbox(&mut draft.sound, "Answer the sound latch with a beep");
            ui.add_space(6.0);
            ui.label(
                RichText::new(
                    "The machine has one sound latch and no waveform: writing it is a note. The host answers with its own bell, which a window started from a desktop does not have.",
                )
                .small()
                .color(MUTED),
            );
        });
    }

    fn page_about(&mut self, ui: &mut egui::Ui) {
        group(ui, "Kestrel Computers", |ui| {
            field(ui, "Version", env!("CARGO_PKG_VERSION"));
            field(ui, "Machines", "Kestrel (1982), Kestrel 16 (1992)");
            field(ui, "Window", "egui, on eframe");
            field(ui, "ROMs", "assembled from roms/*.s in Rust");
        });
        group(ui, "Keys", |ui| {
            field(ui, "F12 or Ctrl+,", "open and close these settings");
            field(ui, "Ctrl+R", "reset the machine");
            field(ui, "Ctrl+P", "pause and resume");
            field(ui, "Ctrl+Q", "quit");
            field(ui, "Pause or Shift+F1", "BREAK");
            field(ui, "Esc", "cancel the settings window");
        });
        group(ui, "Files", |ui| {
            ui.label(
                RichText::new(
                    "Drop a disk, a tape, a cartridge or a ROM on the window and it goes where its size says it belongs.",
                )
                .small()
                .color(MUTED),
            );
            ui.add_space(6.0);
            if ui.button("Commands\u{2026}").clicked() {
                self.help = true;
            }
        });
    }

    // -----------------------------------------------------------------------
    // Media menus, which are also the media pages' actions
    // -----------------------------------------------------------------------

    fn disk_menu(&mut self, ui: &mut egui::Ui) {
        if ui.button("Mount disk image\u{2026}").clicked() {
            self.open_prompt(FileKind::Disk);
        }
        if ui.button("Save disk image\u{2026}").clicked() {
            self.open_prompt(FileKind::SaveDisk);
        }
        if ui.button("Format disk (KESWARP)").clicked() {
            self.machine.bus.disk.format("KESWARP");
            self.set_message("a blank volume, named KESWARP");
        }
        if ui.button("Eject disk").clicked() {
            self.machine.bus.disk.eject();
            self.disk_path = None;
            self.set_message("disk out");
        }
    }

    fn tape_menu(&mut self, ui: &mut egui::Ui) {
        if ui.button("Insert tape\u{2026}").clicked() {
            self.open_prompt(FileKind::Tape);
        }
        if ui.button("Save tape\u{2026}").clicked() {
            self.open_prompt(FileKind::SaveTape);
        }
        if ui.button("Rewind tape").clicked() {
            self.machine.bus.tape.rewind();
            self.set_message("tape rewound");
        }
        if ui.button("Eject tape").clicked() {
            self.machine.bus.tape = kestrel_machine::devices::Tape::new();
            self.tape_path = None;
            self.set_message("tape out");
        }
    }

    fn cartridge_menu(&mut self, ui: &mut egui::Ui) {
        if ui.button("Fit cartridge\u{2026}").clicked() {
            self.open_prompt(FileKind::Cartridge);
        }
        if ui.button("Eject cartridge").clicked() {
            self.machine.bus.cartridge = None;
            self.cartridge_path = None;
            self.set_message("cartridge out");
        }
        ui.separator();
        ui.label(
            RichText::new(format!(
                "{} KiB image",
                self.machine.bus.profile.cartridge_size / 1024
            ))
            .small()
            .color(MUTED),
        );
    }

    // -----------------------------------------------------------------------
    // The file prompt
    // -----------------------------------------------------------------------

    fn open_prompt(&mut self, kind: FileKind) {
        let name = match kind {
            FileKind::SaveDisk => self
                .disk_path
                .as_ref()
                .and_then(|path| path.file_name())
                .map(|name| name.to_string_lossy().into_owned())
                .unwrap_or_else(|| kind.default_name().to_string()),
            FileKind::SaveTape => self
                .tape_path
                .as_ref()
                .and_then(|path| path.file_name())
                .map(|name| name.to_string_lossy().into_owned())
                .unwrap_or_else(|| kind.default_name().to_string()),
            _ => kind.default_name().to_string(),
        };
        let base = match kind {
            FileKind::Roms => self.roms.clone().unwrap_or_else(|| self.last_dir.clone()),
            _ => self.last_dir.clone(),
        };
        let path = if kind == FileKind::Roms {
            base.display().to_string()
        } else if base.as_os_str().is_empty() {
            name
        } else {
            base.join(name).display().to_string()
        };
        self.prompt = Some(Prompt {
            kind,
            title: kind.title().to_string(),
            hint: self.prompt_hint(kind),
            path,
            focus: true,
        });
    }

    fn prompt_hint(&self, kind: FileKind) -> String {
        let profile = self.machine.bus.profile;
        match kind {
            FileKind::Disk => format!(
                "A volume is exactly {} bytes; anything else is refused.",
                self.machine.bus.disk.image_size()
            ),
            FileKind::SaveDisk => "The volume's sectors are written as they stand.".to_string(),
            FileKind::Tape => "Any byte stream goes on the tape.".to_string(),
            FileKind::SaveTape => "What the head has written is written to this file.".to_string(),
            FileKind::Cartridge => format!(
                "A cartridge is exactly {} bytes ({} KiB).",
                profile.cartridge_size,
                profile.cartridge_size / 1024
            ),
            FileKind::Roms => {
                "A directory holding the assembled images, or their sources.".to_string()
            }
            FileKind::Screenshot => "The window's own picture, as a PNG.".to_string(),
            FileKind::SaveConfig => "The switches this window can set, as text.".to_string(),
            FileKind::LoadConfig => "A file this window wrote.".to_string(),
        }
    }

    fn prompt_modal(&mut self, ctx: &egui::Context) {
        let Some(mut prompt) = self.prompt.take() else {
            return;
        };
        let mut close = false;
        let mut accept = false;

        let modal = egui::Modal::new(egui::Id::unique("file-prompt")).show(ctx, |ui| {
            ui.set_width(560.0);
            ui.heading(prompt.title.clone());
            ui.label(RichText::new(prompt.hint.clone()).small().color(MUTED));
            ui.add_space(10.0);
            let edit = ui.add(
                egui::TextEdit::singleline(&mut prompt.path)
                    .desired_width(f32::INFINITY)
                    .hint_text("a path"),
            );
            if prompt.focus {
                edit.request_focus();
                prompt.focus = false;
            }
            if edit.lost_focus() && ui.input(|input| input.key_pressed(egui::Key::Enter)) {
                accept = true;
            }
            ui.add_space(12.0);
            ui.horizontal(|ui| {
                if ui.button(prompt.kind.verb()).clicked() {
                    accept = true;
                }
                if ui.button("Cancel").clicked() {
                    close = true;
                }
            });
        });

        if modal.should_close() {
            close = true;
        }
        if accept {
            let path = PathBuf::from(prompt.path.trim());
            self.perform(ctx, prompt.kind, path);
            return;
        }
        if !close {
            self.prompt = Some(prompt);
        }
    }

    fn perform(&mut self, ctx: &egui::Context, kind: FileKind, path: PathBuf) {
        let name = path
            .file_name()
            .map(|name| name.to_string_lossy().into_owned())
            .unwrap_or_else(|| path.display().to_string());
        match kind {
            FileKind::Disk => match std::fs::read(&path) {
                Ok(bytes) => {
                    if self.machine.bus.disk.load(&bytes) {
                        self.remember(&path);
                        self.disk_path = Some(path);
                        self.set_message(format!("{name} in the drive"));
                    } else {
                        self.set_message(format!(
                            "{name}: {} bytes, not the {} a volume holds",
                            bytes.len(),
                            self.machine.bus.disk.image_size()
                        ));
                    }
                }
                Err(error) => self.set_message(format!("{}: {error}", path.display())),
            },
            FileKind::SaveDisk => match std::fs::write(&path, self.machine.bus.disk.image()) {
                Ok(()) => {
                    self.remember(&path);
                    self.disk_path = Some(path);
                    self.set_message(format!("{name} written"));
                }
                Err(error) => self.set_message(format!("{}: {error}", path.display())),
            },
            FileKind::Tape => match std::fs::read(&path) {
                Ok(bytes) => {
                    self.remember(&path);
                    self.machine.bus.tape.load(&bytes);
                    self.machine.bus.tape.insert();
                    self.tape_path = Some(path);
                    self.set_message(format!("{name} on the tape"));
                }
                Err(error) => self.set_message(format!("{}: {error}", path.display())),
            },
            FileKind::SaveTape => {
                let tape = self.machine.bus.tape.image().to_vec();
                match std::fs::write(&path, tape) {
                    Ok(()) => {
                        self.remember(&path);
                        self.tape_path = Some(path);
                        self.set_message(format!("{name} written"));
                    }
                    Err(error) => self.set_message(format!("{}: {error}", path.display())),
                }
            }
            FileKind::Cartridge => self.fit_cartridge_file(&path),
            FileKind::Roms => {
                self.roms = Some(path.clone());
                self.remember(&path);
                self.reload_roms();
            }
            FileKind::Screenshot => {
                let target = path.clone();
                self.remember(&path);
                ctx.request_screenshot(move |image| write_png(&target, &image, false));
                self.set_message(format!("{name} written"));
            }
            FileKind::SaveConfig => match std::fs::write(&path, self.settings.text()) {
                Ok(()) => {
                    self.remember(&path);
                    self.set_message(format!("{name} written"));
                }
                Err(error) => self.set_message(format!("{}: {error}", path.display())),
            },
            FileKind::LoadConfig => match std::fs::read_to_string(&path) {
                Ok(text) => match Settings::parse(&text) {
                    Ok(settings) => {
                        self.remember(&path);
                        self.apply_settings(settings, ctx);
                        self.set_message(format!("{name} loaded"));
                    }
                    Err(error) => self.set_message(format!("{name}: {error}")),
                },
                Err(error) => self.set_message(format!("{}: {error}", path.display())),
            },
        }
    }

    // -----------------------------------------------------------------------
    // The tube
    // -----------------------------------------------------------------------

    /// The tube: a bezel, the machine's border colour, one rectangle per paper
    /// colour, one run of text per ink colour, and the cursor as a block.
    fn draw_tube(&self, ui: &mut egui::Ui) {
        let columns = self.machine.bus.video.columns() as usize;
        let rows = self.machine.bus.video.rows() as usize;
        let room = (ui.available_size() - Vec2::splat(16.0)).max(Vec2::splat(32.0));

        // How large a glyph is, per point of font size: the metrics are linear,
        // so one measurement serves every size.
        let reference = ui
            .painter()
            .layout_no_wrap("M".to_owned(), FontId::monospace(100.0), Color32::WHITE)
            .size();
        let width_per_point = reference.x / 100.0;
        let height_per_point = reference.y / 100.0;

        // The bezel and the border grow with the font, so the fit is found by
        // asking twice: the first answer sizes them, the second fits inside them.
        let mut font_size: f32 = 12.0;
        for _ in 0..3 {
            let bezel = (font_size * 0.55).clamp(7.0, 26.0);
            let border = if self.settings.show_border {
                (font_size * 0.7).clamp(4.0, 26.0)
            } else {
                0.0
            };
            let chrome = 2.0 * (bezel + border);
            let inner = Vec2::new(
                (room.x - chrome).max(24.0),
                (room.y - chrome).max(24.0),
            );
            let by_width = inner.x / (columns as f32 * width_per_point);
            let by_height =
                ((inner.y / rows as f32) - 1.0).max(1.0) / height_per_point;
            let fit = by_width.min(by_height);
            font_size = (fit * self.settings.zoom / 100.0).clamp(4.0, 72.0);
        }

        let font = FontId::monospace(font_size);
        let glyph = ui
            .painter()
            .layout_no_wrap("M".to_owned(), font.clone(), Color32::WHITE)
            .size();
        let (cell_width, cell_height) = (glyph.x.max(1.0), (glyph.y + 1.0).max(1.0));
        let screen = Vec2::new(cell_width * columns as f32, cell_height * rows as f32);

        let bezel = (font_size * 0.55).clamp(7.0, 26.0);
        let border = if self.settings.show_border {
            (font_size * 0.7).clamp(4.0, 26.0)
        } else {
            0.0
        };
        let outer = screen + Vec2::splat(2.0 * (bezel + border));

        let pad = ((room.y - outer.y) * 0.5).max(0.0);
        ui.add_space(pad);
        ui.vertical_centered(|ui| {
            let (rect, _) = ui.allocate_exact_size(outer, egui::Sense::hover());
            let painter = ui.painter_at(rect);
            let colours = palette(self.machine.model());

            // The case: a dark bezel, a hairline where the light catches it.
            painter.rect_filled(rect, CornerRadius::same(14), BEZEL);
            painter.rect_stroke(
                rect,
                CornerRadius::same(14),
                Stroke::new(1.0, EDGE),
                StrokeKind::Inside,
            );
            painter.line_segment(
                [
                    Pos2::new(rect.left() + 14.0, rect.top() + 1.0),
                    Pos2::new(rect.right() - 14.0, rect.top() + 1.0),
                ],
                Stroke::new(1.0, Color32::from_white_alpha(10)),
            );

            // The ring the machine itself paints, in its own border colour.
            let ring = rect.shrink(bezel);
            if self.settings.show_border {
                let colour = colours[(self.machine.bus.border & 0x0F) as usize];
                painter.rect_filled(ring, CornerRadius::same(3), colour);
            }

            let screen_rect = ring.shrink(border);
            painter.rect_filled(screen_rect, 0.0, colours[0]);
            let cells = painter.with_clip_rect(screen_rect);

            for row in 0..rows {
                let mut column = 0;
                while column < columns {
                    let index = row * columns + column;
                    let attribute = self.machine.bus.video.attribute(index);
                    // A run of the same attribute is one rect and one piece of text.
                    let mut end = column + 1;
                    while end < columns
                        && self.machine.bus.video.attribute(row * columns + end) == attribute
                    {
                        end += 1;
                    }
                    let origin = Pos2::new(
                        screen_rect.left() + column as f32 * cell_width,
                        screen_rect.top() + row as f32 * cell_height,
                    );
                    let span = Rect::from_min_size(
                        origin,
                        egui::vec2((end - column) as f32 * cell_width, cell_height),
                    );
                    let paper = colours[(attribute & 0x0F) as usize];
                    let ink = colours[(attribute >> 4) as usize];
                    cells.rect_filled(span, 0.0, paper);
                    let text: String = (column..end)
                        .map(|index| {
                            let code = self.machine.bus.video.cell(row * columns + index);
                            if (32..127).contains(&code) {
                                code as char
                            } else {
                                ' '
                            }
                        })
                        .collect();
                    if !text.trim().is_empty() {
                        cells.text(origin, egui::Align2::LEFT_TOP, text, font.clone(), ink);
                    }
                    column = end;
                }
            }

            if self.settings.show_cursor && self.machine.bus.cursor_on {
                let (row, column) = (
                    self.machine.bus.cursor_row as usize,
                    self.machine.bus.cursor_column as usize,
                );
                if row < rows && column < columns {
                    let attribute = self.machine.bus.video.attribute(row * columns + column);
                    let origin = Pos2::new(
                        screen_rect.left() + column as f32 * cell_width,
                        screen_rect.top() + row as f32 * cell_height,
                    );
                    cells.rect_filled(
                        Rect::from_min_size(origin, egui::vec2(cell_width, cell_height)),
                        0.0,
                        colours[(attribute >> 4) as usize],
                    );
                }
            }
        });
    }

    fn help_window(&mut self, ctx: &egui::Context) {
        if !self.help {
            return;
        }
        let mut open = self.help;
        egui::Window::new("Commands")
            .open(&mut open)
            .default_width(560.0)
            .default_height(460.0)
            .resizable(true)
            .show(ctx, |ui| {
                ui.label(
                    RichText::new(machine_name(self.machine.model()))
                        .strong()
                        .color(ACCENT),
                );
                ui.add_space(6.0);
                let words: &[(&str, &str)] = match self.machine.model() {
                    Model::Kestrel => KESTREL_WORDS,
                    Model::Kestrel16 => KESTREL16_WORDS,
                };
                egui::ScrollArea::vertical().show(ui, |ui| {
                    egui::Grid::new("commands")
                        .num_columns(2)
                        .striped(true)
                        .spacing([18.0, 6.0])
                        .show(ui, |ui| {
                            for (word, meaning) in words {
                                ui.label(RichText::new(*word).monospace().color(TEXT));
                                ui.label(RichText::new(*meaning).small().color(MUTED));
                                ui.end_row();
                            }
                        });
                });
            });
        self.help = open;
    }
}

impl eframe::App for App {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();

        if ctx.input(|input| input.key_pressed(egui::Key::F12)) {
            if self.settings_open() {
                self.draft = None;
            } else {
                self.open_settings();
            }
        }
        if self.settings_open() && ctx.input(|input| input.key_pressed(egui::Key::Escape)) {
            self.draft = None;
            self.set_message("settings put back");
        }

        self.prompt_modal(&ctx);
        if self.prompt.is_none() && !self.settings_open() {
            self.take_keys(&ctx);
        }
        self.tick();

        // A file dropped on the window is hardware being plugged in.
        let dropped = ctx.input(|input| input.raw.dropped_files.clone());
        for file in dropped {
            let path = file.path().to_path_buf();
            let name = path
                .file_name()
                .map(|name| name.to_string_lossy().into_owned())
                .unwrap_or_else(|| path.display().to_string());
            match file.bytes() {
                Ok(bytes) => self.accept_dropped(&bytes, &name, Some(path.clone())),
                Err(error) => self.set_message(format!("{name}: {error}")),
            }
        }

        // A second of the machine's own time, then a photograph: the banner has
        // finished by then, which is what a picture of a machine should show.
        if let Some(path) = self.shot.take_if(|_| self.frames_run >= 50) {
            let target = path.clone();
            ctx.request_screenshot(move |image| write_png(&target, &image, true));
            self.shot = Some(path);
        }

        self.menus(ui);

        egui::Panel::bottom("status")
            .frame(
                egui::Frame::new()
                    .fill(PANEL)
                    .inner_margin(Margin::symmetric(12, 5)),
            )
            .show(ui, |ui| self.status_bar(ui));

        egui::CentralPanel::default()
            .frame(egui::Frame::new().fill(BG).inner_margin(Margin::ZERO))
            .show(ui, |ui| {
                egui::ScrollArea::both()
                    .auto_shrink([false, false])
                    .show(ui, |ui| self.draw_tube(ui));
            });

        self.settings_window(&ctx);
        self.help_window(&ctx);

        // Fifty frames a second is the machines' own idea of time.
        ctx.request_repaint_after(Duration::from_millis(16));
    }
}

const KESTREL_WORDS: &[(&str, &str)] = &[
    ("LIST [n]", "the program, or from line n"),
    ("RUN", "run the stored program"),
    ("NEW", "throw the program away"),
    ("DELETE n[,m]", "remove lines"),
    ("SAVE \"NAME\"", "the program to the disk"),
    ("LOAD \"NAME\"", "the program from the disk"),
    ("CATALOG", "what is on the volume"),
    ("PRINT", "expressions and strings, ? is shorthand"),
    ("INPUT", "a value from the keyboard"),
    ("GET", "one key, without waiting for Return"),
    ("LET A=41", "a variable, A to Z"),
    ("DIM A(10)", "an array"),
    ("READ / DATA / RESTORE", "a list written in the program"),
    ("IF ... THEN ... ELSE", "one branch or the other"),
    ("FOR ... NEXT", "a loop, with STEP"),
    ("GOTO / GOSUB / RETURN", "jumps and subroutines"),
    ("ON ... GOTO / GOSUB", "a choice of jumps"),
    ("REM / END / STOP / CONT", "remarks and stopping"),
    ("POKE addr,value", "a byte of memory"),
    ("CLS", "clear the screen"),
    ("HELP", "this list"),
    ("DEF FN A(X)=...", "a function of your own, then FN A(3)"),
];

const KESTREL16_WORDS: &[(&str, &str)] = &[
    ("PRINT", "expressions and strings, ? is shorthand"),
    ("RUN [n] / LIST [n]", "run the program, or show it"),
    ("GOTO n / GOSUB n / RETURN", "jumps and sixteen levels of subroutine"),
    ("IF cond THEN ... [ELSE ...]", "one branch or the other"),
    ("FOR v=a TO b [STEP s] ... NEXT v", "a loop"),
    ("LET A=41 / A=41", "variables A to Z, sixteen bits"),
    ("LET A$=\"text\"", "a string variable, thirty-one characters"),
    ("LEN / CHR$ / ASC", "strings as numbers and numbers as strings"),
    ("LEFT$ / RIGHT$ / MID$", "parts of a string"),
    ("STR$ / VAL", "digits and the number they begin with"),
    ("ABS / SGN", "the size without its sign, and the sign alone"),
    ("DEF FNA(X)=expr / FNA(n)", "a function of the program's own"),
    ("DIM A(10) / DIM M(2,3)", "arrays of whole words"),
    ("DIM S$(10) / S$(i)", "arrays of strings"),
    ("PEEK(addr) / POKE addr,value", "a byte of bank zero"),
    ("FIND \"NAME\"", "where a file is on the volume"),
    ("SAVE / LOAD / DELETE", "the program store and DOS 2.0"),
    ("CSAVE / CLOAD", "the program to and from the cassette"),
    ("CLS / REM / END / NEW", "the screen, a remark, the line, a fresh start"),
];

// ---------------------------------------------------------------------------
// Small things the window draws
// ---------------------------------------------------------------------------

fn badge(ui: &mut egui::Ui, text: &str, colour: Color32) {
    egui::Frame::new()
        .fill(colour.gamma_multiply(0.16))
        .stroke(Stroke::new(1.0, colour.gamma_multiply(0.55)))
        .corner_radius(CornerRadius::same(6))
        .inner_margin(Margin::symmetric(8, 2))
        .show(ui, |ui| {
            ui.label(RichText::new(text).small().strong().color(colour));
        });
}

fn dot(ui: &mut egui::Ui) {
    ui.label(RichText::new("\u{2022}").small().color(EDGE));
}

fn lamp(ui: &mut egui::Ui, label: &str, on: bool, tip: &str) {
    let (rect, response) = ui.allocate_exact_size(Vec2::new(9.0, 9.0), egui::Sense::hover());
    ui.painter()
        .circle_filled(rect.center(), 4.0, if on { ACCENT } else { LAMP_OFF });
    response.on_hover_text(tip);
    ui.label(
        RichText::new(label)
            .small()
            .color(if on { TEXT } else { MUTED }),
    );
}

fn field(ui: &mut egui::Ui, label: &str, value: &str) {
    ui.horizontal(|ui| {
        ui.label(RichText::new(label).small().color(MUTED));
        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
            ui.label(RichText::new(value).small().color(TEXT));
        });
    });
}

/// A titled group, which is the shape every page is built out of.
fn group(ui: &mut egui::Ui, title: &str, add: impl FnOnce(&mut egui::Ui)) {
    egui::Frame::new()
        .fill(WELL)
        .stroke(Stroke::new(1.0, EDGE))
        .corner_radius(CornerRadius::same(8))
        .inner_margin(Margin::symmetric(12, 10))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.label(
                RichText::new(title.to_uppercase())
                    .small()
                    .strong()
                    .color(ACCENT),
            );
            ui.add_space(6.0);
            add(ui);
        });
    ui.add_space(10.0);
}

fn machine_card(ui: &mut egui::Ui, selected: bool, title: &str, subtitle: &str) -> bool {
    let fill = if selected {
        ACCENT.gamma_multiply(0.12)
    } else {
        WELL
    };
    let stroke = if selected {
        Stroke::new(1.5, ACCENT)
    } else {
        Stroke::new(1.0, EDGE)
    };
    let response = egui::Frame::new()
        .fill(fill)
        .stroke(stroke)
        .corner_radius(CornerRadius::same(8))
        .inner_margin(Margin::symmetric(14, 10))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.horizontal(|ui| {
                ui.vertical(|ui| {
                    ui.label(RichText::new(title).strong().size(15.0).color(TEXT));
                    ui.label(RichText::new(subtitle).small().color(MUTED));
                });
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    if selected {
                        ui.label(RichText::new("\u{25CF}").color(ACCENT));
                    }
                });
            });
        })
        .response;
    response.interact(egui::Sense::click()).clicked()
}

fn swatch(ui: &mut egui::Ui, colour: Color32, index: usize) {
    let (rect, response) = ui.allocate_exact_size(Vec2::splat(18.0), egui::Sense::hover());
    ui.painter().rect_filled(rect, CornerRadius::same(3), colour);
    ui.painter().rect_stroke(
        rect,
        CornerRadius::same(3),
        Stroke::new(1.0, EDGE),
        StrokeKind::Inside,
    );
    response.on_hover_text(format!("colour {index}"));
}

fn cpu_name(model: Model) -> &'static str {
    match model {
        Model::Kestrel => "6502",
        Model::Kestrel16 => "65816",
    }
}

fn machine_name(model: Model) -> &'static str {
    match model {
        Model::Kestrel => "Kestrel",
        Model::Kestrel16 => "Kestrel 16",
    }
}

fn size_text(bytes: usize) -> String {
    if bytes >= 1024 * 1024 {
        format!("{} MiB", bytes / (1024 * 1024))
    } else {
        format!("{} KiB", bytes / 1024)
    }
}

fn window_title(model: Model) -> String {
    format!("Kestrel Computers \u{2014} {}", machine_name(model))
}

fn default_window(model: Model) -> Vec2 {
    // A tube is forty or eighty columns of a monospace glyph, so it stands a
    // little taller than it is wide; the window is sized to it and no more.
    match model {
        Model::Kestrel => Vec2::new(640.0, 780.0),
        Model::Kestrel16 => Vec2::new(620.0, 780.0),
    }
}

/// A machine with its ROMs in it, reset and at the prompt.
fn boot(model: Model, roms: Option<PathBuf>) -> Result<Machine, String> {
    let mut machine = Machine::new(model);
    match roms.as_deref() {
        Some(directory) => machine.mount_shipped_roms(directory),
        None => machine.mount_assembled_roms(),
    }
    .map_err(|error| error.to_string())?;
    machine.reset();
    Ok(machine)
}

fn write_png(path: &Path, image: &egui::ColorImage, leave: bool) {
    let (width, height) = (image.width(), image.height());
    let bytes = image.as_raw().to_vec();
    match image::save_buffer(
        path,
        &bytes,
        width as u32,
        height as u32,
        image::ColorType::Rgba8,
    ) {
        Ok(()) => eprintln!("kestrel_computers: wrote {}", path.display()),
        Err(error) => eprintln!(
            "kestrel_computers: cannot write {}: {error}",
            path.display()
        ),
    }
    if leave {
        std::process::exit(0);
    }
}

fn apply_theme(ctx: &egui::Context) {
    let mut visuals = egui::Visuals::dark();
    visuals.panel_fill = PANEL;
    visuals.window_fill = PANEL;
    visuals.extreme_bg_color = BG;
    visuals.faint_bg_color = RAISED;
    visuals.code_bg_color = RAISED;
    visuals.window_stroke = Stroke::new(1.0, EDGE);
    visuals.window_corner_radius = CornerRadius::same(10);
    visuals.menu_corner_radius = CornerRadius::same(8);
    visuals.window_shadow = egui::Shadow {
        offset: [0, 10],
        blur: 26,
        spread: 0,
        color: Color32::from_black_alpha(150),
    };
    visuals.popup_shadow = egui::Shadow {
        offset: [0, 6],
        blur: 18,
        spread: 0,
        color: Color32::from_black_alpha(130),
    };
    visuals.selection.bg_fill = Color32::from_rgb(0x27, 0x5C, 0x52);
    visuals.selection.stroke = Stroke::new(1.0, ACCENT);
    visuals.widgets.noninteractive.bg_fill = PANEL;
    visuals.widgets.noninteractive.bg_stroke = Stroke::new(1.0, EDGE);
    visuals.widgets.noninteractive.fg_stroke = Stroke::new(1.0, TEXT);
    visuals.widgets.inactive.bg_fill = RAISED;
    visuals.widgets.inactive.weak_bg_fill = RAISED;
    visuals.widgets.inactive.bg_stroke = Stroke::new(1.0, EDGE);
    visuals.widgets.inactive.fg_stroke = Stroke::new(1.0, TEXT);
    visuals.widgets.hovered.bg_fill = HOVER;
    visuals.widgets.hovered.weak_bg_fill = HOVER;
    visuals.widgets.hovered.bg_stroke = Stroke::new(1.0, ACCENT.gamma_multiply(0.5));
    visuals.widgets.hovered.fg_stroke = Stroke::new(1.0, Color32::WHITE);
    visuals.widgets.active.bg_fill = PRESSED;
    visuals.widgets.active.weak_bg_fill = PRESSED;
    visuals.widgets.active.bg_stroke = Stroke::new(1.0, ACCENT);
    visuals.widgets.active.fg_stroke = Stroke::new(1.0, Color32::WHITE);
    visuals.widgets.open.bg_fill = HOVER;
    visuals.widgets.open.weak_bg_fill = HOVER;
    ctx.set_visuals(visuals);

    ctx.global_style_mut(|style| {
        style.spacing.item_spacing = Vec2::new(8.0, 6.0);
        style.spacing.button_padding = Vec2::new(10.0, 4.0);
        style.spacing.menu_margin = Margin::same(8);
        style.spacing.menu_spacing = 4.0;
        style.spacing.indent = 16.0;
        style.spacing.slider_width = 150.0;
        style.spacing.combo_width = 150.0;
    });
}

fn main() -> eframe::Result<()> {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    let mut model = Model::Kestrel;
    let mut images_arg: Option<PathBuf> = None;
    let mut shot: Option<PathBuf> = None;
    let mut cartridge: Option<String> = None;
    let mut language: Option<String> = None;
    let mut index = 0;
    while index < arguments.len() {
        match arguments[index].as_str() {
            "--model" => {
                index += 1;
                model = match arguments.get(index).map(String::as_str) {
                    Some("kestrel") => Model::Kestrel,
                    Some("kestrel16") | Some("kestrel-16") => Model::Kestrel16,
                    other => {
                        eprintln!("kestrel_computers: no such model: {other:?}");
                        std::process::exit(2);
                    }
                };
            }
            "--roms" => {
                index += 1;
                images_arg = arguments.get(index).map(PathBuf::from);
            }
            "--cart" => {
                index += 1;
                cartridge = arguments.get(index).cloned();
            }
            "--rom" => {
                index += 1;
                language = arguments.get(index).cloned();
            }
            "--shot" => {
                index += 1;
                shot = arguments.get(index).map(PathBuf::from);
            }
            "--help" | "-h" => {
                println!(
                    "usage: kestrel_computers [--model kestrel|kestrel16] [--rom NAME]\n\
                     \x20                         [--roms DIR] [--cart NAME|FILE] [--shot PNG]\n\
                     \n\
                     \x20 --model   which machine to power on (default kestrel)\n\
                     \x20 --roms    where the assembled images are (default build/generated)\n\
                     \x20 --rom     which language to put in the machine, by name from roms/\n\
                     \x20 --cart    a cartridge to fit, by name from cartridges/ or as an image\n\
                     \x20 --shot    write a PNG of the window once it has settled, then leave"
                );
                return Ok(());
            }
            other => {
                eprintln!("kestrel_computers: unknown option: {other}");
                std::process::exit(2);
            }
        }
        index += 1;
    }

    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size(default_window(model))
            .with_min_inner_size([560.0, 420.0])
            .with_title(window_title(model)),
        ..Default::default()
    };
    let mut app = match App::new(model, images_arg) {
        Ok(app) => app,
        Err(message) => {
            eprintln!("kestrel_computers: {message}");
            std::process::exit(1);
        }
    };
    if let Some(which) = &language {
        app.mount_socket_rom(which);
    }
    if let Some(which) = &cartridge {
        app.fit_cartridge_argument(which);
    }
    app.shot = shot.clone();
    eframe::run_native(
        "Kestrel Computers",
        options,
        Box::new(move |cc| {
            apply_theme(&cc.egui_ctx);
            Ok(Box::new(app) as Box<dyn eframe::App>)
        }),
    )
}
