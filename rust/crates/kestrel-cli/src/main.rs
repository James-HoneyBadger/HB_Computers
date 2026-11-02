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
//           [--file PATH] [--roms DIR] [--no-screen]

use std::path::PathBuf;
use std::process::ExitCode;

use kestrel_machine::machine::Machine;
use kestrel_machine::profile::Model;

struct Options {
    model: Model,
    frames: i32,
    lines: Vec<String>,
    roms: Option<PathBuf>,
    screen: bool,
    disk: Option<PathBuf>,
    tape: Option<PathBuf>,
}

fn usage() {
    println!(
        "usage: kestrel [--model kestrel|kestrel16] [--frames N] [--type \"LINE\"]...\n\
         \x20              [--file PATH] [--roms DIR] [--no-screen]\n\
         \n\
         \x20 --model    which machine to power on (default kestrel)\n\
         \x20 --frames   frames to run before and after typing (default 40)\n\
         \x20 --type     a line to type at the machine; may be repeated\n\
         \x20 --file     a file of lines to type, one to a line\n\
         \x20 --roms     where the assembled images are (default build/generated)\n\
         \x20 --disk     a disk image to put in the drive; it is written back at exit\n\
         \x20 --tape     a tape image to put in the deck\n\
         \x20 --no-screen  run without printing the tube\n\
         \n\
         The images are assembled by the C++ build: run ./start.sh build first."
    );
}

fn parse(arguments: &[String]) -> Result<Options, String> {
    let mut options = Options {
        model: Model::Kestrel,
        frames: 40,
        lines: Vec::new(),
        roms: None,
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
            "--roms" => options.roms = Some(PathBuf::from(value()?)),
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
