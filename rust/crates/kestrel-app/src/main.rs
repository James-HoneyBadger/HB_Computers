// SPDX-License-Identifier: MIT
//
// kestrel_computers -- the machines' window, in egui.
//
// One executable, two machines, because they are one emulator: `--model kestrel`
// powers on the 1982 machine, `--model kestrel16` the 1992 one.
//
//   kestrel_computers                     the Kestrel
//   kestrel_computers --model kestrel16   the Kestrel 16
//
// The window is a tube, a keyboard and a few switches. Everything on the tube is
// machine code in a ROM image; the window only decides how many cycles pass and
// what colour each attribute byte is.

use std::path::PathBuf;
use std::time::{Duration, Instant};

use eframe::egui::{self, Color32, FontId, Pos2, Rect, Stroke};
use kestrel_machine::machine::{shipped_roms_directory, Machine};
use kestrel_machine::profile::Model;

/// The machines' sixteen colours, as the C++ window's palette has them.
const PALETTE: [Color32; 16] = [
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

#[derive(Clone, Copy, PartialEq, Eq)]
enum Speed {
    Mhz1,
    Mhz2,
    Mhz4,
    Mhz8,
    Turbo,
}

impl Speed {
    fn label(self) -> &'static str {
        match self {
            Speed::Mhz1 => "1 MHz",
            Speed::Mhz2 => "2 MHz",
            Speed::Mhz4 => "4 MHz",
            Speed::Mhz8 => "8 MHz",
            Speed::Turbo => "Turbo",
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
}

struct App {
    machine: Machine,
    images: PathBuf,
    speed: Speed,
    last: Instant,
    /// What the window last said, so a message can be read without leaving.
    message: String,
}

impl App {
    fn new(model: Model, images: PathBuf, speed: Speed) -> Result<Self, String> {
        let mut machine = Machine::new(model);
        machine
            .mount_shipped_roms(&images)
            .map_err(|error| error.to_string())?;
        machine.reset();
        Ok(Self {
            machine,
            images,
            speed,
            last: Instant::now(),
            message: String::new(),
        })
    }

    /// Power a machine on. A different model is a different machine, so this is a
    /// cold boot and anything in memory is lost; the same model is a reset.
    fn select_model(&mut self, model: Model) {
        if model == self.machine.model() {
            self.machine.reset();
            self.message = "cold booted".to_string();
            return;
        }
        match App::new(model, self.images.clone(), self.speed) {
            Ok(app) => {
                self.machine = app.machine;
                self.message = "powered on".to_string();
            }
            Err(message) => self.message = message,
        }
    }

    fn mount_socket_rom(&mut self, id: &str) {
        if self.machine.model() != Model::Kestrel {
            self.message = "the Kestrel 16's language ROM is a bank, not a socket".to_string();
            return;
        }
        let path = self.images.join("kestrel").join(format!("{id}.bin"));
        match std::fs::read(&path) {
            Ok(image) => match self.machine.mount_basic(&image) {
                Ok(()) => {
                    self.machine.reset();
                    self.message = format!("{id} in the socket");
                }
                Err(error) => self.message = error.to_string(),
            },
            Err(error) => self.message = format!("no such ROM: {id} ({error})"),
        }
    }

    /// A file dropped on the window goes where its size says it belongs: a
    /// cartridge is the profile's cartridge size, a socket ROM the language's,
    /// and anything else is a tape.
    fn accept_dropped(&mut self, bytes: &[u8], name: &str) {
        let profile = self.machine.bus.profile;
        if bytes.len() == profile.cartridge_size as usize {
            match self.machine.bus.cartridge.as_mut() {
                Some(slot) => {
                    slot.copy_from_slice(bytes);
                    self.message = format!("{name} in the cartridge slot");
                }
                None => {
                    self.machine.bus.cartridge = Some(bytes.to_vec());
                    self.message = format!("{name} in the cartridge slot");
                }
            }
        } else if bytes.len() == profile.basic_size as usize
            && self.machine.model() == Model::Kestrel
        {
            match self.machine.mount_basic(bytes) {
                Ok(()) => {
                    self.machine.reset();
                    self.message = format!("{name} in the socket");
                }
                Err(error) => self.message = error.to_string(),
            }
        } else {
            self.machine.bus.tape.load(bytes);
            self.machine.bus.tape.insert();
            self.message = format!("{name} on the tape");
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
                        self.message = "BREAK".to_string();
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
        match self.speed {
            Speed::Turbo => {
                let slice = self.machine.bus.profile.cycles_per_frame as i64 * 2;
                self.machine.run_cycles(slice);
            }
            _ => {
                let cycles = elapsed.as_secs_f64() * self.speed.cycles_per_second();
                if cycles >= 1.0 {
                    self.machine.run_cycles(cycles as i64);
                }
            }
        }
    }

    fn menus(&mut self, ui: &mut egui::Ui) {
        egui::Panel::top("menus").show(ui, |ui| {
            egui::MenuBar::new().ui(ui, |ui| {
                ui.menu_button("Machine", |ui| {
                    let model = self.machine.model();
                    if ui
                        .selectable_label(model == Model::Kestrel, "Kestrel (1982)")
                        .clicked()
                    {
                        self.select_model(Model::Kestrel);
                    }
                    if ui
                        .selectable_label(model == Model::Kestrel16, "Kestrel 16 (1992)")
                        .clicked()
                    {
                        self.select_model(Model::Kestrel16);
                    }
                    ui.separator();
                    if ui.button("BREAK").clicked() {
                        self.machine.press_break();
                        self.message = "BREAK".to_string();
                    }
                });
                ui.menu_button("Speed", |ui| {
                    for speed in [
                        Speed::Mhz1,
                        Speed::Mhz2,
                        Speed::Mhz4,
                        Speed::Mhz8,
                        Speed::Turbo,
                    ] {
                        if ui
                            .selectable_label(self.speed == speed, speed.label())
                            .clicked()
                        {
                            self.speed = speed;
                        }
                        if speed == Speed::Mhz8 {
                            ui.separator();
                        }
                    }
                });
                if self.machine.model() == Model::Kestrel {
                    ui.menu_button("Socket ROM", |ui| {
                        if ui.button("BASIC V2.0").clicked() {
                            self.mount_socket_rom("basic");
                        }
                        if ui.button("MONITOR V1.0").clicked() {
                            self.mount_socket_rom("monitor");
                        }
                    });
                }
                ui.menu_button("Tape", |ui| {
                    if ui.button("Stop").clicked() {
                        self.machine.bus.tape.stop();
                        self.message = "tape out".to_string();
                    }
                });
                ui.separator();
                ui.label(
                    egui::RichText::new(format!(
                        "{} \u{2014} {} \u{2014} frame {}",
                        match self.machine.model() {
                            Model::Kestrel => "Kestrel",
                            Model::Kestrel16 => "Kestrel 16",
                        },
                        self.speed.label(),
                        self.machine.bus.frame_counter
                    ))
                    .weak(),
                );
            });
        });
    }

    /// The tube: one rectangle per paper colour, one run of text per ink colour,
    /// and the cursor as a block over the cell it is on.
    fn draw_tube(&self, ui: &mut egui::Ui) {
        let columns = self.machine.bus.video.columns() as usize;
        let rows = self.machine.bus.video.rows() as usize;
        // The tube fills the window: the glyph is as large as the narrower of the
        // two fits allows, so forty columns and eighty both read.
        let room = ui.available_size();
        let font_size = (room.x / (columns as f32 * 0.62)).min(room.y / (rows as f32 * 1.25));
        let font = FontId::monospace(font_size.clamp(6.0, 30.0));
        let glyph = ui
            .painter()
            .layout_no_wrap("M".to_string(), font.clone(), Color32::WHITE)
            .size();
        let (cell_width, cell_height) = (glyph.x, glyph.y + 1.0);
        let size = egui::vec2(cell_width * columns as f32, cell_height * rows as f32);
        let (rect, _) = ui.allocate_exact_size(size, egui::Sense::click());
        let painter = ui.painter_at(rect);
        painter.rect_filled(rect, 0.0, PALETTE[0]);

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
                    rect.left() + column as f32 * cell_width,
                    rect.top() + row as f32 * cell_height,
                );
                let span = Rect::from_min_size(
                    origin,
                    egui::vec2((end - column) as f32 * cell_width, cell_height),
                );
                let paper = PALETTE[(attribute & 0x0F) as usize];
                let ink = PALETTE[(attribute >> 4) as usize];
                painter.rect_filled(span, 0.0, paper);
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
                if text.trim().is_empty() {
                    painter.rect_filled(span, 0.0, paper);
                } else {
                    painter.text(origin, egui::Align2::LEFT_TOP, text, font.clone(), ink);
                }
                column = end;
            }
        }

        if self.machine.bus.cursor_on {
            let (row, column) = (
                self.machine.bus.cursor_row as usize,
                self.machine.bus.cursor_column as usize,
            );
            if row < rows && column < columns {
                let attribute =
                    self.machine.bus.video.attribute(row * columns + column);
                let origin = Pos2::new(
                    rect.left() + column as f32 * cell_width,
                    rect.top() + row as f32 * cell_height,
                );
                painter.rect_filled(
                    Rect::from_min_size(origin, egui::vec2(cell_width, cell_height)),
                    0.0,
                    PALETTE[(attribute >> 4) as usize],
                );
            }
        }

        painter.rect_stroke(
            rect,
            0.0,
            Stroke::new(1.0, Color32::from_gray(90)),
            egui::StrokeKind::Outside,
        );
    }
}

impl eframe::App for App {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        self.take_keys(&ctx);
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
                Ok(bytes) => self.accept_dropped(&bytes, &name),
                Err(error) => self.message = format!("{name}: {error}"),
            }
        }

        self.menus(ui);
        egui::Panel::bottom("status").show(ui, |ui| {
            ui.horizontal(|ui| {
                ui.label(format!(
                    "{} Hz  \u{2022}  frame {}  \u{2022}  {} cycles",
                    self.machine.bus.profile.clock_hz,
                    self.machine.bus.frame_counter,
                    self.machine.cpu.cycles()
                ));
                if !self.message.is_empty() {
                    ui.separator();
                    ui.label(egui::RichText::new(&self.message).weak());
                }
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    ui.label(
                        egui::RichText::new("drop a cartridge, a socket ROM or a tape on the window")
                            .weak()
                            .small(),
                    );
                });
            });
        });
        egui::CentralPanel::default().show(ui, |ui| {
            ui.vertical_centered(|ui| self.draw_tube(ui));
        });

        // Fifty frames a second is the machines' own idea of time.
        ctx.request_repaint_after(Duration::from_millis(16));
    }
}

fn main() -> eframe::Result<()> {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    let mut model = Model::Kestrel;
    let mut images_arg: Option<PathBuf> = None;
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
            "--help" | "-h" => {
                println!(
                    "usage: kestrel_computers [--model kestrel|kestrel16] [--roms DIR]\n\
                     \n\
                     \x20 --model   which machine to power on (default kestrel)\n\
                     \x20 --roms    where the assembled images are (default build/generated)"
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

    let images = match shipped_roms_directory(images_arg.as_deref()) {
        Ok(path) => path,
        Err(message) => {
            eprintln!("kestrel_computers: {message}");
            std::process::exit(1);
        }
    };

    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([720.0, 560.0])
            .with_title("Kestrel Computers"),
        ..Default::default()
    };
    let app = match App::new(model, images, Speed::Mhz1) {
        Ok(app) => app,
        Err(message) => {
            eprintln!("kestrel_computers: {message}");
            std::process::exit(1);
        }
    };
    eframe::run_native(
        "Kestrel Computers",
        options,
        Box::new(move |_cc| Ok(Box::new(app) as Box<dyn eframe::App>)),
    )
}
