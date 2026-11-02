// SPDX-License-Identifier: MIT
//
// kestrel -- one emulator, two machines, on a shell.
//
// The same program boots the Kestrel or the Kestrel 16, because they are the same
// machine with different profiles. Type at it, let it run, print the screen.
// Everything on that screen came out of the model's ROMs: this file is the hands,
// not the machine.
//
//   kestrel [--model kestrel|kestrel16] [--frames N] [--type "LINE"]...
//           [--file PATH] [--rom NAME|basic] [--roms DIR] [--cart NAME|FILE]
//           [--disk IMAGE] [--tape IMAGE] [--no-screen]

use std::path::PathBuf;
use std::process::ExitCode;

use kestrel_machine::machine::Machine;
use kestrel_machine::profile::Model;

struct Options {
    model: Model,
    frames: i32,
    lines: Vec<String>,
    rom: Option<String>,
    roms: Option<PathBuf>,
    cartridge: Option<String>,
    screen: bool,
    disk: Option<PathBuf>,
    tape: Option<PathBuf>,
}

fn usage() {
    println!(
        "usage: kestrel [--model kestrel|kestrel16] [--frames N] [--type \"LINE\"]...\n\
         \x20              [--file PATH] [--rom NAME] [--roms DIR] [--languages]\n\
         \x20              [--cart NAME|FILE] [--disk IMAGE] [--tape IMAGE]\n\
         \x20              [--no-screen]\n\
         \n\
         \x20 --model    which machine to power on (default kestrel)\n\
         \x20 --frames   frames to run before and after typing (default 40)\n\
         \x20 --type     a line to type at the machine; may be repeated\n\
         \x20 --file     a file of lines to type, one to a line\n\
         \x20 --rom      which language to put in the machine: one of the names\n\
         \x20            `--languages` lists (basic when the machine starts)\n\
         \x20 --languages  list the language ROMs the repository ships for the model\n\
         \x20 --roms     a directory of assembled images; without it the ROMs are\n\
         \x20            assembled from roms/*.s when the machine starts\n\
         \x20 --cart     a cartridge to fit: one of the names `--cartridges` lists,\n\
         \x20            assembled from cartridges/*.s, or a file holding an image\n\
         \x20 --cartridges  list the cartridges the repository ships for the model\n\
         \x20 --disk     a disk image to put in the drive; it is written back at exit\n\
         \x20 --tape     a tape image to put in the deck\n\
         \x20 --no-screen  run without printing the tube"
    );
}

fn parse(arguments: &[String]) -> Result<Options, String> {
    let mut options = Options {
        model: Model::Kestrel,
        frames: 40,
        lines: Vec::new(),
        rom: None,
        roms: None,
        cartridge: None,
        screen: true,
        disk: None,
        tape: None,
    };
    let mut index = 0;
    while index < arguments.len() {
        let argument = arguments[index].as_str();
        let mut value = || -> Result<String, String> {
            index += 1;
            arguments
                .get(index)
                .cloned()
                .ok_or_else(|| format!("{argument} needs a value"))
        };
        match argument {
            "--model" => {
                let name = value()?;
                options.model = match name.as_str() {
                    "kestrel" => Model::Kestrel,
                    "kestrel16" | "kestrel-16" => Model::Kestrel16,
                    other => return Err(format!("no such model: {other}")),
                };
            }
            "--frames" => {
                let count = value()?;
                options.frames = count
                    .parse()
                    .map_err(|_| format!("not a number of frames: {count}"))?;
            }
            "--type" => options.lines.push(value()?),
            "--file" => {
                let path = value()?;
                let text = std::fs::read_to_string(&path)
                    .map_err(|error| format!("cannot read {path}: {error}"))?;
                options.lines.extend(text.lines().map(str::to_string));
            }
            "--rom" => options.rom = Some(value()?),
            "--roms" => options.roms = Some(PathBuf::from(value()?)),
            "--languages" => {
                for name in kestrel_machine::roms::shipped_languages(options.model)
                    .map_err(|error| error.to_string())?
                {
                    println!("{name}");
                }
                return Err(String::new());
            }
            "--cart" => options.cartridge = Some(value()?),
            "--cartridges" => {
                let names = kestrel_machine::roms::shipped_cartridges(options.model)
                    .map_err(|error| error.to_string())?;
                if names.is_empty() {
                    println!("no cartridges for this model");
                }
                for name in names {
                    println!("{name}");
                }
                return Err(String::new());
            }
            "--disk" => options.disk = Some(PathBuf::from(value()?)),
            "--tape" => options.tape = Some(PathBuf::from(value()?)),
            "--no-screen" => options.screen = false,
            "--help" | "-h" => {
                usage();
                return Err(String::new());
            }
            other => return Err(format!("unknown option: {other}")),
        }
        index += 1;
    }
    Ok(options)
}

fn run(options: &Options) -> Result<(), String> {
    let mut machine = Machine::new(options.model);
    match options.roms.as_deref() {
        Some(directory) => machine
            .mount_shipped_roms(directory)
            .map_err(|error| error.to_string())?,
        None => machine
            .mount_assembled_roms()
            .map_err(|error| error.to_string())?,
    }
    if let Some(id) = &options.rom {
        let image = socket_rom(options, id)?;
        machine
            .mount_basic(&image)
            .map_err(|error| error.to_string())?;
    }
    machine.reset();
    if let Some(path) = &options.disk {
        machine
            .mount_disk_image(path)
            .map_err(|error| format!("{}: {error}", path.display()))?;
    }
    if let Some(path) = &options.tape {
        machine
            .mount_tape_image(path)
            .map_err(|error| format!("{}: {error}", path.display()))?;
    }
    if let Some(cartridge) = &options.cartridge {
        let path = PathBuf::from(cartridge);
        if path.is_file() {
            machine
                .mount_cartridge(&path)
                .map_err(|error| format!("{}: {error}", path.display()))?;
        } else {
            machine
                .mount_assembled_cartridge(cartridge)
                .map_err(|error| format!("cartridge {cartridge}: {error}"))?;
        }
        // The reset is what fits a cartridge: it is looked at once, on the way up.
        machine.reset();
    }
    machine.run_frames(options.frames);

    for line in &options.lines {
        machine.type_line_paced(line);
    }
    // A program worth watching takes longer than the typing did.
    machine.run_frames(options.frames);

    if options.screen {
        println!("{}", machine.screen_text());
    }
    // The disk is a disk: what the machine wrote is there next time it is
    // mounted.
    if let Some(path) = &options.disk {
        machine
            .save_disk_image(path)
            .map_err(|error| format!("{}: {error}", path.display()))?;
    }
    Ok(())
}

/// The image `--rom` asks for: the assembled one, or the shipped file when
/// `--roms` named a directory of them. The Kestrel's socket and the Kestrel 16's
/// language bank both take one language at a time, and the names come from the
/// sources in `roms/`, so a machine's catalogue is what it can be asked for.
fn socket_rom(options: &Options, id: &str) -> Result<Vec<u8>, String> {
    let names = kestrel_machine::roms::shipped_languages(options.model)?;
    if !names.iter().any(|name| name == id) {
        return Err(match options.model {
            Model::Kestrel => format!("no such ROM: {id}; the Kestrel has {}", names.join(", ")),
            Model::Kestrel16 => format!(
                "no such ROM: {id}; the Kestrel 16's language is a bank, not a socket, and it \
                 holds {}",
                names.join(", ")
            ),
        });
    }
    match options.roms.as_deref() {
        Some(root) => {
            let folder = match options.model {
                Model::Kestrel => "kestrel",
                Model::Kestrel16 => "kestrel16",
            };
            let path = root.join(folder).join(format!("{id}.bin"));
            std::fs::read(&path).map_err(|error| format!("{}: {error}", path.display()))
        }
        None => kestrel_machine::roms::language(options.model, id),
    }
}

fn main() -> ExitCode {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    let options = match parse(&arguments) {
        Ok(options) => options,
        Err(message) => {
            if !message.is_empty() {
                eprintln!("kestrel: {message}");
                usage();
            }
            return ExitCode::from(2);
        }
    };
    match run(&options) {
        Ok(()) => ExitCode::SUCCESS,
        Err(message) => {
            eprintln!("kestrel: {message}");
            ExitCode::from(1)
        }
    }
}
