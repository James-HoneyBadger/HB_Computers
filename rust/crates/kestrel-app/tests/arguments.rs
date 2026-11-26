// SPDX-License-Identifier: MIT
use std::process::Command;

#[test]
fn missing_option_values_fail_before_opening_window() {
    for option in ["--roms", "--cart", "--rom", "--shot"] {
        let output = Command::new(env!("CARGO_BIN_EXE_kestrel_computers"))
            .arg(option).output().unwrap();
        assert_eq!(output.status.code(), Some(2), "{option}");
    }
}
