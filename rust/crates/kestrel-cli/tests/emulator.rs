// SPDX-License-Identifier: MIT
//
// The interactive emulator, asked the questions the C++ `kestrel_emu_tests` asks
// it: does it boot the model it was told to, switch models, answer the session
// commands, and type at the machine in a way a person would recognise.

use std::io::Write;
use std::process::{Command, Stdio};

/// Run a script at the emulator and return what it printed.
fn session(script: &str, arguments: &[&str]) -> String {
    let mut child = Command::new(env!("CARGO_BIN_EXE_kestrel_emu"))
        .args(arguments)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("the emulator binary");
    child
        .stdin
        .as_mut()
        .expect("stdin")
        .write_all(script.as_bytes())
        .expect("could not write the script");
    let output = child.wait_with_output().expect("the emulator to finish");
    String::from_utf8_lossy(&output.stdout).into_owned()
        + &String::from_utf8_lossy(&output.stderr)
}

#[test]
fn the_1982_machine_boots_and_runs_a_line() {
    let out = session("PRINT 2+2*3\n/screen\n", &["--model", "kestrel"]);
    assert!(out.contains("KESTREL BASIC"), "{out}");
    assert!(
        out.lines().any(|row| row.trim_matches(|c| c == '|' || c == ' ').trim() == "8"),
        "the answer on the tube:\n{out}"
    );
}

#[test]
fn the_1992_machine_boots_and_runs_a_line() {
    let out = session("PRINT 1000*20\n/screen\n", &["--model", "kestrel16"]);
    assert!(out.contains("KESTREL 16 BASIC"), "{out}");
    assert!(out.contains("20000"), "{out}");
}

#[test]
fn the_model_can_be_switched_in_the_middle_of_a_session() {
    // A different model is a different machine, and the emulator says so.
    let out = session("/model kestrel16\n/screen\n", &["--model", "kestrel"]);
    assert!(out.contains("Kestrel 16 powered on"), "{out}");
    assert!(out.contains("KESTREL 16 BASIC"), "the tube changed machine:\n{out}");
    // The same model again is a cold boot.
    let out = session("/model kestrel\n", &["--model", "kestrel"]);
    assert!(out.contains("Kestrel cold booted"), "{out}");
}

#[test]
fn the_session_commands_answer() {
    let out = session("/help\n/frames 60\n/frames nought\n/status\n/what\n", &["--model", "kestrel"]);
    for wanted in [
        "/model kestrel|kestrel16",
        "60 frames a line",
        "/frames wants a number above zero",
        "1000000 Hz",
        "cycles",
        "no such command: /what",
    ] {
        assert!(out.contains(wanted), "no {wanted:?} in:\n{out}");
    }
}

#[test]
fn the_break_key_starts_the_machine_again() {
    let out = session("/break\n/screen\n", &["--model", "kestrel"]);
    // A warm start says READY again rather than showing the greeting twice.
    assert!(out.contains("READY"), "{out}");
}

#[test]
fn a_socket_rom_can_be_chosen_and_a_bank_cannot() {
    let out = session("/rom basic\n/screen\n", &["--model", "kestrel"]);
    assert!(out.contains("KESTREL BASIC"), "{out}");
    let out = session("/rom monitor\n/screen\n", &["--model", "kestrel"]);
    assert!(!out.contains("KESTREL BASIC"), "the socket changed:\n{out}");
    let out = session("/rom basic\n", &["--model", "kestrel16"]);
    assert!(out.contains("is a bank, not a socket"), "{out}");
    let out = session("/rom nonsense\n", &["--model", "kestrel"]);
    assert!(out.contains("no such ROM: nonsense"), "{out}");
}
