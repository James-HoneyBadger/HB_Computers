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

#[test]
fn a_disk_and_a_tape_can_be_put_in_and_taken_out() {
    let directory = std::env::temp_dir().join("kestrel-emu-media");
    std::fs::create_dir_all(&directory).expect("a directory for the media");
    let disk = directory.join("demo.dsk");
    let tape = directory.join("demo.tap");
    std::fs::write(&disk, vec![0u8; 512 * 256]).expect("a disk image");
    std::fs::write(&tape, [1u8, 2, 3]).expect("a tape image");

    let script = format!(
        "/disk {}\n/tape {}\n/disk\n/tape\n/disk {}/missing.dsk\n",
        disk.display(),
        tape.display(),
        directory.display()
    );
    let out = session(&script, &["--model", "kestrel"]);
    assert!(out.contains("demo.dsk in the drive"), "{out}");
    assert!(out.contains("demo.tap on the tape"), "{out}");
    assert!(out.contains("demo.dsk written"), "the volume went back out:\n{out}");
    assert!(out.contains("demo.tap written"), "{out}");
    assert!(out.contains("No such file"), "a missing image is reported:\n{out}");
    assert_eq!(std::fs::metadata(&disk).expect("the disk").len(), 512 * 256);
    assert_eq!(std::fs::read(&tape).expect("the tape"), vec![1, 2, 3]);
    let _ = std::fs::remove_dir_all(&directory);
}

/// A model switch is not an eject: the disk a person put in is still in the drive
/// afterwards, when the new drive can take it. It used to be lost, which made the
/// machine's filesystem answer "no disk" for reasons that had nothing to do with
/// the disk.
#[test]
fn the_media_survive_a_model_switch() {
    let directory = std::env::temp_dir().join("kestrel-emu-media-switch");
    std::fs::create_dir_all(&directory).expect("a directory for the media");
    // The 1992 drive takes 4096 sectors of 512 bytes; a medium that fits the
    // machine being booted is the medium that stays in it.
    let disk = directory.join("switch.dsk");
    std::fs::write(&disk, vec![0u8; 4096 * 512]).expect("a disk image");

    let script = format!("/disk {}\n/model kestrel16\n/disk\n", disk.display());
    let out = session(&script, &["--model", "kestrel16"]);
    assert!(out.contains("in the drive"), "{out}");
    assert!(
        out.contains("written"),
        "the volume was still in the drive after the switch:\n{out}"
    );
    let _ = std::fs::remove_dir_all(&directory);
}
