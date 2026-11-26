// SPDX-License-Identifier: MIT
use std::process::Command;

fn run(args: &[&str]) -> std::process::Output {
    Command::new(env!("CARGO_BIN_EXE_kestrel")).args(args).output().unwrap()
}

#[test]
fn help_succeeds() {
    assert!(run(&["--help"]).status.success());
}

#[test]
fn catalogues_succeed_and_use_the_final_model() {
    let before = run(&["--model", "kestrel16", "--languages"]);
    let after = run(&["--languages", "--model", "kestrel16"]);
    assert!(before.status.success());
    assert!(after.status.success());
    assert_eq!(before.stdout, after.stdout);
    assert_eq!(String::from_utf8_lossy(&after.stdout).trim(), "basic");
    assert!(run(&["--cartridges"]).status.success());
}

#[test]
fn invalid_arguments_fail() {
    for args in [&["--frames", "-1"][..], &["--frames", "0"], &["--model", "unknown"], &["--unknown"]] {
        assert_eq!(run(args).status.code(), Some(2));
    }
}


#[test]
fn interactive_catalogue_is_order_independent() {
    let run = |args: &[&str]| Command::new(env!("CARGO_BIN_EXE_kestrel_emu")).args(args).output().unwrap();
    let before = run(&["--model", "kestrel16", "--languages"]);
    let after = run(&["--languages", "--model", "kestrel16"]);
    assert!(before.status.success() && after.status.success());
    assert_eq!(before.stdout, after.stdout);
    assert_eq!(String::from_utf8_lossy(&after.stdout).trim(), "basic");
}

#[test]
fn interactive_invalid_frames_and_missing_rom_path_fail() {
    for args in [&["--frames", "-1"][..], &["--frames", "0"], &["--frames", "bad"], &["--frames"], &["--roms"]] {
        let out = Command::new(env!("CARGO_BIN_EXE_kestrel_emu")).args(args).output().unwrap();
        assert_eq!(out.status.code(), Some(2));
    }
}
