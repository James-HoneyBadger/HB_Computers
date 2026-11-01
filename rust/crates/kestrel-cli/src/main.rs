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

use std::path::{Path, PathBuf};
use std::process::ExitCode;

use kestrel_machine::machine::Machine;
use kestrel_machine::profile::Model;

struct Options {
    model: Model,
    frames: i32,
    lines: Vec<String>,
    roms: Option<PathBuf>,
    screen: bool,
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

/// Where the assembled images are. The C++ build puts them in
/// `build/generated`; a checkout that has never been built has none, which is
/// worth saying plainly rather than failing with a missing file.
fn roms_directory(chosen: Option<&PathBuf>) -> Result<PathBuf, String> {
    if let Some(path) = chosen {
        return Ok(path.clone());
    }
    if let Some(path) = std::env::var_os("KESTREL_ROMS") {
        return Ok(PathBuf::from(path));
    }
    let candidates = [
        PathBuf::from("build/generated"),
        Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../build/generated"),
    ];
    for candidate in candidates {
        if candidate.join("kestrel").join("kernal.bin").is_file() {
            return Ok(candidate);
        }
    }
    Err("no assembled ROMs: run ./start.sh build, or say where they are with --roms".to_string())
}

fn run(options: &Options) -> Result<(), String> {
    let images = roms_directory(options.roms.as_ref())?;
    let mut machine = Machine::new(options.model);
    machine
        .mount_shipped_roms(&images)
        .map_err(|error| error.to_string())?;
    machine.reset();
    machine.run_frames(options.frames);

    for line in &options.lines {
        machine.type_line_paced(line);
    }
    // A program worth watching takes longer than the typing did.
    machine.run_frames(options.frames);

    if options.screen {
        println!("{}", machine.screen_text());
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
