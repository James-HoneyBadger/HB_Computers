// SPDX-License-Identifier: MIT
//
// kestrel-emu -- an interactive emulator that runs either machine.
//
// One program, two models, chosen at the start or switched in the middle of a
// session. The machine itself is the library, so this file is only the hands: it
// powers a model on, types at it, lets it run and shows the tube.
//
//   kestrel-emu                      asks which machine, when there is a keyboard
//   kestrel-emu --model kestrel16    powers on the 1992 machine
//   kestrel-emu --type "PRINT 2+2"   one line, then the screen
//
// A line beginning with `/` is a command for this program rather than the
// machine; everything else is typed at the machine. A session whose input is a
// pipe -- a script -- draws when it asks with /screen and once at the end,
// because a screen after every line is noise when nobody is watching.

use std::io::{BufRead, IsTerminal, Write};
use std::path::PathBuf;
use std::process::ExitCode;

use kestrel_machine::machine::{Cpu, Machine, KEY_RETURN};
use kestrel_machine::profile::Model;

fn model_name(model: Model) -> &'static str {
    match model {
        Model::Kestrel => "Kestrel",
        Model::Kestrel16 => "Kestrel 16",
    }
}

struct Emulator {
    model: Model,
    machine: Machine,
    frames: i32,
    quiet: bool,
    /// The disk in the drive and the tape in the deck, when there is one.
    disk_path: Option<PathBuf>,
    tape_path: Option<PathBuf>,
    /// Where images to load from disk live, when the session was told to use
    /// them; otherwise the ROMs are assembled from their sources.
    roms: Option<PathBuf>,
}

impl Emulator {
    fn boot(model: Model, roms: Option<PathBuf>, frames: i32, quiet: bool) -> Result<Self, String> {
        let mut machine = Machine::new(model);
        match roms.as_deref() {
            Some(directory) => machine
                .mount_shipped_roms(directory)
                .map_err(|error| error.to_string())?,
            None => machine
                .mount_assembled_roms()
                .map_err(|error| error.to_string())?,
        }
        machine.reset();
        machine.run_frames(20);
        Ok(Self {
            model,
            machine,
            frames,
            quiet,
            disk_path: None,
            tape_path: None,
            roms,
        })
    }

    /// Power a machine on. A different model is a different machine, so this is a
    /// cold boot and anything in memory is lost; the same model is a reset.
    fn select_model(&mut self, model: Model) -> Result<bool, String> {
        let different = model != self.model;
        // A different machine is a different drive and a different deck, but the
        // *media* are still in them: the person put them there, and switching
        // model is not ejecting. Losing them silently made a CATALOG read a drive
        // that was empty.
        let disk = self.disk_path.clone();
        let tape = self.tape_path.clone();
        *self = Self::boot(model, self.roms.clone(), self.frames, self.quiet)?;
        if let Some(path) = disk {
            if self.machine.mount_disk_image(&path).is_ok() {
                self.disk_path = Some(path);
            }
        }
        if let Some(path) = tape {
            if self.machine.mount_tape_image(&path).is_ok() {
                self.tape_path = Some(path);
            }
        }
        Ok(different)
    }

    fn mount_socket_rom(&mut self, id: &str) -> Result<(), String> {
        if self.model != Model::Kestrel {
            return Err("the Kestrel 16's language ROM is a bank, not a socket".to_string());
        }
        let shipped = kestrel_machine::roms::shipped(Model::Kestrel)?;
        let image = match id {
            "basic" => &shipped.basic,
            "monitor" => shipped
                .monitor
                .as_ref()
                .ok_or("this machine has basic and monitor")?,
            other => return Err(format!("no such ROM: {other}; this machine has basic and monitor")),
        };
        self.machine
            .mount_basic(image)
            .map_err(|error| error.to_string())?;
        self.machine.reset();
        self.machine.run_frames(20);
        Ok(())
    }

    /// Type one line and let the machine answer. The keys go in at the speed the
    /// machine could be typed at: a line pushed in at once overflows the
    /// controller and loses the tail, the Return included.
    fn type_line(&mut self, text: &str) {
        for byte in text.bytes() {
            while self.machine.bus.keyboard.len() >= 8 {
                self.machine.run_frames(1);
            }
            self.machine.type_key(byte);
            self.machine.run_frames(2);
        }
        while self.machine.bus.keyboard.len() >= 8 {
            self.machine.run_frames(1);
        }
        self.machine.type_key(KEY_RETURN);

        // A machine in a loop of its own stops reading keys, and waiting for it
        // would hang the terminal. Say so, and leave BREAK to the person.
        let mut waited = 0;
        while !self.machine.bus.keyboard.is_empty() {
            self.machine.run_frames(1);
            waited += 1;
            if waited > 500 {
                println!(
                    "kestrel_emu: the machine is not reading its keyboard; it may be \
                     looping (/break interrupts it)"
                );
                break;
            }
        }
        self.machine.run_frames(self.frames);
    }

    /// The tube, without the blank tail, which is what a terminal can show.
    /// `forced` is for /screen and for the last screen of a session.
    fn draw(&self, forced: bool) {
        if self.quiet && !forced {
            return;
        }
        let rows = self.machine.bus.video.rows() as usize;
        let columns = self.machine.bus.video.columns() as usize;
        let last = (0..rows).filter(|row| !self.machine.screen_line(*row).is_empty()).last();
        let rule = format!("+{}+", "-".repeat(columns));
        println!("{rule}");
        if let Some(last) = last {
            for row in 0..=last {
                println!("|{}|", self.machine.screen_line(row));
            }
        }
        println!("{rule}");
    }

    fn status(&self) {
        let clock = self.machine.bus.profile.clock_hz;
        let frame = self.machine.bus.frame_counter;
        match self.machine.cpu {
            Cpu::Mos6502(ref cpu) => println!(
                "{}: {clock} Hz, frame {frame}, {} cycles, {} opcodes",
                model_name(self.model),
                self.machine.cpu.cycles(),
                cpu.instruction_count()
            ),
            Cpu::Mos65816(_) => println!(
                "{}: {clock} Hz, frame {frame}, {} cycles",
                model_name(self.model),
                self.machine.cpu.cycles()
            ),
        }
    }
}

fn help(frames: i32) {
    println!(
        "  /model kestrel|kestrel16   power on that machine (cold boot)\n\
         \x20 /speed 1|2|4|8|turbo      noted; a session runs frames, not a clock\n\
         \x20 /frames N                 frames run after each line (now {frames})\n\
         \x20 /run [N]                  run N frames (default 50)\n\
         \x20 /break                    the BREAK key\n\
         \x20 /rom basic|monitor        the Kestrel's socket ROM\n\
         \x20 /disk [FILE]              mount a volume, or write the one in the drive back\n\
         \x20 /tape [FILE]              insert a cassette, or write the one in the deck back\n\
         \x20 /screen                   draw the tube\n\
         \x20 /status                   clock, frame, cycles\n\
         \x20 /help                     this list\n\
         \x20 /quit                     leave\n\
         anything else is typed at the machine"
    );
}

/// A command, or `false` if the line is for the machine.
fn command(emulator: &mut Emulator, line: &str, quit: &mut bool) -> bool {
    let Some(rest) = line.strip_prefix('/') else {
        return false;
    };
    let (name, argument) = match rest.find(' ') {
        Some(space) => (&rest[..space], rest[space + 1..].trim()),
        None => (rest, ""),
    };
    match name {
        "quit" | "q" => *quit = true,
        "help" | "h" => help(emulator.frames),
        "model" => match argument {
            "kestrel" | "kestrel16" | "kestrel-16" => {
                let wanted = if argument == "kestrel" {
                    Model::Kestrel
                } else {
                    Model::Kestrel16
                };
                match emulator.select_model(wanted) {
                    Ok(different) => {
                        let what = if different { "powered on" } else { "cold booted" };
                        println!("kestrel_emu: {} {what}", model_name(wanted));
                        emulator.draw(false);
                    }
                    Err(message) => println!("kestrel_emu: {message}"),
                }
            }
            _ => println!("kestrel_emu: /model wants kestrel or kestrel16"),
        },
        // The host runs frames as fast as it is asked to, so a speed is an
        // intention here; the window is where it becomes a throttle.
        "speed" => match argument {
            "1" | "2" | "4" | "8" => println!("kestrel_emu: {argument} MHz"),
            "turbo" => println!("kestrel_emu: as fast as this computer"),
            _ => println!("kestrel_emu: /speed wants 1, 2, 4, 8 or turbo"),
        },
        "frames" => match argument.parse::<i32>() {
            Ok(count) if count > 0 => {
                emulator.frames = count;
                println!("kestrel_emu: {count} frames a line");
            }
            _ => println!("kestrel_emu: /frames wants a number above zero"),
        },
        "run" => {
            let frames = argument.parse::<i32>().unwrap_or(50);
            emulator.machine.run_frames(if frames > 0 { frames } else { 50 });
            emulator.draw(false);
        }
        "break" => {
            emulator.machine.press_break();
            emulator.machine.run_frames(20);
            emulator.draw(false);
        }
        // A disk in the drive and a tape in the deck. `/disk FILE` mounts and
        // loads; `/disk` on its own writes the volume back out, which is what a
        // person does before switching the machine off.
        "disk" => match argument {
            "" => match emulator.disk_path.clone() {
                Some(path) => match emulator.machine.save_disk_image(&path) {
                    Ok(()) => println!("kestrel_emu: {} written", path.display()),
                    Err(error) => println!("kestrel_emu: {}: {error}", path.display()),
                },
                None => println!("kestrel_emu: no disk in the drive; /disk FILE mounts one"),
            },
            path => {
                let path = PathBuf::from(path);
                match emulator.machine.mount_disk_image(&path) {
                    Ok(()) => {
                        emulator.disk_path = Some(path.clone());
                        println!("kestrel_emu: {} in the drive", path.display());
                    }
                    Err(error) => println!("kestrel_emu: {}: {error}", path.display()),
                }
            }
        },
        "tape" => match argument {
            "" => match emulator.tape_path.clone() {
                Some(path) => match emulator.machine.save_tape_image(&path) {
                    Ok(()) => println!("kestrel_emu: {} written", path.display()),
                    Err(error) => println!("kestrel_emu: {}: {error}", path.display()),
                },
                None => println!("kestrel_emu: no tape in the deck; /tape FILE mounts one"),
            },
            path => {
                let path = PathBuf::from(path);
                match emulator.machine.mount_tape_image(&path) {
                    Ok(()) => {
                        emulator.tape_path = Some(path.clone());
                        println!("kestrel_emu: {} on the tape", path.display());
                    }
                    Err(error) => println!("kestrel_emu: {}: {error}", path.display()),
                }
            }
        },
        "rom" => match emulator.mount_socket_rom(argument) {
            Ok(()) => emulator.draw(false),
            Err(message) => println!("kestrel_emu: {message}"),
        },
        "screen" => emulator.draw(true),
        "status" => emulator.status(),
        _ => println!("kestrel_emu: no such command: /{name} (try /help)"),
    }
    true
}

fn ask_which_machine() -> Model {
    let mut answer = String::new();
    println!("which machine?");
    println!("  1  Kestrel (1982, BASIC V2.0 in the socket)");
    print!("  2  Kestrel 16 (1992)   [1] ");
    let _ = std::io::stdout().flush();
    let _ = std::io::stdin().read_line(&mut answer);
    if answer.trim() == "2" {
        Model::Kestrel16
    } else {
        Model::Kestrel
    }
}

fn usage() {
    println!(
        "usage: kestrel-emu [--model kestrel|kestrel16] [--speed 1|2|4|8|turbo]\n\
         \x20                  [--frames N] [--type \"LINE\"]... [--rom basic|monitor]\n\
         \x20                  [--roms DIR]\n\
         \n\
         \x20 --model   which machine to power on; without it, it asks\n\
         \x20 --speed   noted for the session; a session runs frames, not a clock\n\
         \x20 --frames  frames run after each line typed (default 20)\n\
         \x20 --type    a line to type before the session starts; may be repeated\n\
         \x20 --rom     which ROM is in the Kestrel's socket: basic or monitor\n\
         \x20 --roms    images to load from disk instead of assembling the ROMs\n\
         \n\
         Inside a session, a line beginning with / is a command: /help lists them."
    );
}

fn main() -> ExitCode {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    let mut model: Option<Model> = None;
    let mut frames = 20;
    let mut lines: Vec<String> = Vec::new();
    let mut images_arg: Option<PathBuf> = None;
    let mut rom: Option<String> = None;

    let mut index = 0;
    while index < arguments.len() {
        let argument = arguments[index].as_str();
        let value = |index: &mut usize| -> Option<String> {
            *index += 1;
            arguments.get(*index).cloned()
        };
        match argument {
            "--model" => {
                model = match value(&mut index).as_deref() {
                    Some("kestrel") => Some(Model::Kestrel),
                    Some("kestrel16") | Some("kestrel-16") => Some(Model::Kestrel16),
                    Some(other) => {
                        eprintln!("kestrel_emu: no such model: {other}");
                        return ExitCode::from(2);
                    }
                    None => {
                        eprintln!("kestrel_emu: --model needs a name");
                        return ExitCode::from(2);
                    }
                };
            }
            "--frames" => {
                frames = value(&mut index).and_then(|text| text.parse().ok()).unwrap_or(20);
            }
            "--type" => match value(&mut index) {
                Some(text) => lines.push(text),
                None => {
                    eprintln!("kestrel_emu: --type needs a line");
                    return ExitCode::from(2);
                }
            },
            "--speed" => {
                let _ = value(&mut index);
            }
            "--rom" => match value(&mut index) {
                Some(id) => rom = Some(id),
                None => {
                    eprintln!("kestrel_emu: --rom needs a name");
                    return ExitCode::from(2);
                }
            },
            "--roms" => images_arg = value(&mut index).map(PathBuf::from),
            "--help" | "-h" => {
                usage();
                return ExitCode::SUCCESS;
            }
            other => {
                eprintln!("kestrel_emu: unknown option: {other}");
                usage();
                return ExitCode::from(2);
            }
        }
        index += 1;
    }

    let interactive = std::io::stdin().is_terminal();
    let chosen = model.unwrap_or_else(|| {
        if interactive {
            ask_which_machine()
        } else {
            Model::Kestrel
        }
    });

    let mut emulator =
        match Emulator::boot(chosen, images_arg.clone(), frames, !interactive) {
            Ok(emulator) => emulator,
            Err(message) => {
                eprintln!("kestrel_emu: {message}");
                return ExitCode::from(1);
            }
        };
    if let Some(id) = &rom {
        if let Err(message) = emulator.mount_socket_rom(id) {
            eprintln!("kestrel_emu: {message}");
            return ExitCode::from(2);
        }
    }
    for line in &lines {
        emulator.type_line(line);
    }

    let stdin = std::io::stdin();
    let mut quit = false;
    if interactive {
        println!(
            "{} powered on. /help lists the commands; a line without a\n\
             leading / is typed at the machine.",
            model_name(emulator.model)
        );
        emulator.draw(false);
        let mut input = String::new();
        while !quit {
            print!("/> ");
            let _ = std::io::stdout().flush();
            input.clear();
            match stdin.lock().read_line(&mut input) {
                Ok(0) | Err(_) => break,
                Ok(_) => {}
            }
            let line = input.trim_end_matches(['\r', '\n']).to_string();
            if line.is_empty() {
                continue;
            }
            if command(&mut emulator, &line, &mut quit) {
                continue;
            }
            emulator.type_line(&line);
            emulator.draw(false);
        }
    } else {
        // Nothing to prompt: read whatever arrives, then show the machine. This
        // is what makes the emulator scriptable, and why a script's screens are
        // the ones it asks for and the last one.
        let mut input = String::new();
        while !quit {
            input.clear();
            match stdin.lock().read_line(&mut input) {
                Ok(0) | Err(_) => break,
                Ok(_) => {}
            }
            let line = input.trim_end_matches(['\r', '\n']).to_string();
            if line.is_empty() {
                continue;
            }
            if !command(&mut emulator, &line, &mut quit) {
                emulator.type_line(&line);
            }
        }
        if !quit {
            emulator.draw(true);
        }
    }
    ExitCode::SUCCESS
}
