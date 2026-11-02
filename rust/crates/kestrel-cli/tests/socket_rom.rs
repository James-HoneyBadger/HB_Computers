// SPDX-License-Identifier: MIT
//
// `--rom` on the command line: the Kestrel's socket takes one 16 KiB image at a
// time, and the names come from the sources in `roms/`, so the machine is asked
// for one of its own.

use std::process::Command;

/// Run `kestrel` with the given arguments and return everything it printed.
fn run(arguments: &[&str]) -> String {
    let output = Command::new(env!("CARGO_BIN_EXE_kestrel"))
        .args(arguments)
        .output()
        .expect("the command line binary");
    String::from_utf8_lossy(&output.stdout).into_owned()
        + &String::from_utf8_lossy(&output.stderr)
}

#[test]
fn the_monitor_can_be_put_in_the_socket_from_the_command_line() {
    let out = run(&["--rom", "monitor", "--type", "R"]);
    assert!(
        out.contains("KESTREL MONITOR"),
        "the monitor did not boot:\n{out}"
    );
    assert!(
        !out.contains("KESTREL BASIC"),
        "BASIC was still in the socket:\n{out}"
    );
    assert!(out.contains("A="), "the register dump is missing:\n{out}");
}

#[test]
fn a_socket_rom_that_does_not_exist_is_refused() {
    let out = run(&["--rom", "nonsense"]);
    assert!(out.contains("no such ROM: nonsense"), "{out}");
}

#[test]
fn the_kestrel_16_has_no_socket_to_choose_from() {
    let out = run(&["--model", "kestrel16", "--rom", "monitor"]);
    assert!(out.contains("is a bank, not a socket"), "{out}");
}

#[test]
fn the_languages_are_what_the_sources_hold() {
    let out = run(&["--languages"]);
    let names: Vec<&str> = out.lines().map(str::trim).filter(|line| !line.is_empty()).collect();
    assert_eq!(names, ["basic", "diagnostics", "monitor", "pilot"], "{out}");
    let out = run(&["--model", "kestrel16", "--languages"]);
    assert!(out.contains("basic"), "{out}");
    assert!(!out.contains("pilot"), "the Kestrel 16 has no pilot:\n{out}");
}
