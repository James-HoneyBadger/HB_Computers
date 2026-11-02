// SPDX-License-Identifier: MIT
//
// `--rom` on the command line: the Kestrel's socket takes one 16 KiB image, and
// the Kestrel 16's language is a bank, so asking it for a socket ROM is refused.

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
