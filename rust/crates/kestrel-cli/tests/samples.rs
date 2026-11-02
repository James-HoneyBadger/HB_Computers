// SPDX-License-Identifier: MIT
//
// Every sample program, run through the Rust machine and checked against the
// same `.expected` file the C++ suite uses. This is the port's strongest
// agreement test: twenty-four programs typed at a machine whose language is
// machine code assembled from the shared `roms/`, and the answers compared with
// the answers the C++ tree has already agreed to.
//
// A checkout that has never been built has no ROMs, and these tests say so
// rather than passing quietly.

use std::path::{Path, PathBuf};

use kestrel_machine::machine::Machine;
use kestrel_machine::profile::Model;

fn repository() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../..")
}

fn lines(path: &Path) -> Vec<String> {
    std::fs::read_to_string(path)
        .unwrap_or_else(|error| panic!("{}: {error}", path.display()))
        .lines()
        .map(str::to_string)
        .collect()
}

/// One sample: boot the model, type the file at it, run it, and look for every
/// line of the expected output on the tube.
fn run_sample(sample: &Path, model: Model) {
    let expected = sample.with_extension("expected");
    let mut machine = Machine::new(model);
    machine
        .mount_assembled_roms()
        .expect("the ROM sources to assemble");
    machine.reset();
    machine.run_frames(25);

    for line in lines(sample) {
        machine.type_line_paced(&line);
    }
    match model {
        // The 1982 machine stores numbered lines; they have to be run.
        Model::Kestrel => {
            machine.type_line_paced("RUN");
            machine.run_frames(400);
        }
        Model::Kestrel16 => machine.run_frames(40),
    }

    let screen = machine.screen_text();
    for wanted in lines(&expected) {
        assert!(
            on_screen(&machine, &wanted),
            "{}: the tube never showed {:?}\n{}\n{}",
            sample.display(),
            wanted,
            "-".repeat(60),
            screen
        );
    }
}

/// The convention the C++ samples suite uses, and the same one here: a line
/// beginning with `~` has to appear somewhere on the tube -- for a row that runs
/// past the columns a person reads -- and any other line has to be a whole row of
/// the tube, exactly.
fn on_screen(machine: &Machine, wanted: &str) -> bool {
    match wanted.strip_prefix('~') {
        Some(rest) => machine.screen_text().contains(rest),
        None => (0..machine.bus.video.rows() as usize)
            .any(|row| machine.screen_line(row) == wanted),
    }
}

fn run_directory(name: &str, model: Model) {
    let directory = repository().join("samples").join(name);
    let mut samples: Vec<PathBuf> = std::fs::read_dir(&directory)
        .unwrap_or_else(|error| panic!("{}: {error}", directory.display()))
        .filter_map(|entry| entry.ok().map(|entry| entry.path()))
        .filter(|path| path.extension().is_some_and(|extension| extension == "bas"))
        .collect();
    samples.sort();
    assert!(!samples.is_empty(), "no samples in {}", directory.display());
    for sample in samples {
        run_sample(&sample, model);
    }
}

#[test]
fn the_1982_samples_run() {
    run_directory("kestrel", Model::Kestrel);
}

#[test]
fn the_1992_samples_run() {
    run_directory("kestrel16", Model::Kestrel16);
}
