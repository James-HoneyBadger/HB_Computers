// SPDX-License-Identifier: MIT
//
// The language ROMs the repository ships: the socket takes one 16 KiB image at a
// time, the Kestrel 16's language is a 64 KiB bank, and every source in `roms/`
// that is not the KERNAL is one of them. This is the test that the catalogue,
// the sizes and the service ROM all hold together.

use kestrel_machine::machine::Machine;
use kestrel_machine::profile::Model;
use kestrel_machine::roms;

#[test]
fn every_language_fills_the_socket_it_is_for() {
    for model in [Model::Kestrel, Model::Kestrel16] {
        let names = roms::shipped_languages(model).expect("a directory of ROM sources");
        assert!(!names.is_empty(), "no languages for {model:?}");
        assert_eq!(names[0], "basic", "BASIC is the language in the box");
        let wanted = match model {
            Model::Kestrel => 16 * 1024,
            Model::Kestrel16 => 64 * 1024,
        };
        for name in names {
            let image = roms::language(model, &name)
                .unwrap_or_else(|error| panic!("{name} for {model:?}: {error}"));
            assert_eq!(image.len(), wanted, "{name} does not fill its socket");
        }
    }
}

#[test]
fn the_kernal_is_not_in_the_catalogue() {
    let names = roms::shipped_languages(Model::Kestrel).expect("a directory of ROM sources");
    assert!(!names.iter().any(|name| name == "kernal"), "{names:?}");
}

#[test]
fn the_1982_machine_is_sold_with_basic_and_a_monitor_and_more_besides() {
    let names = roms::shipped_languages(Model::Kestrel).expect("a directory of ROM sources");
    for wanted in ["basic", "monitor", "pilot", "diagnostics"] {
        assert!(
            names.iter().any(|name| name == wanted),
            "the {wanted} ROM is missing from {names:?}"
        );
    }
}

/// The diagnostics ROM is the one that reports on the machine rather than using
/// it, so this is the test that it measures something and says so.
#[test]
fn the_diagnostics_rom_tests_the_machine_and_reports() {
    let mut machine = Machine::new(Model::Kestrel);
    machine.mount_assembled_roms().expect("the ROMs to assemble");
    machine
        .mount_assembled_language("diagnostics")
        .expect("the diagnostics ROM to assemble");
    machine.reset();
    // A memory test of thirty kilobytes is several seconds of a one-megahertz
    // machine; the frame budget is what that costs.
    machine.run_frames(400);
    let screen = machine.screen_text();
    assert!(screen.contains("KESTREL DIAGNOSTICS V1.0"), "{screen}");
    assert!(screen.contains("RAM  $0800-$7FFF  OK"), "{screen}");
    assert!(screen.contains("TUBE 40 X 25      OK"), "{screen}");
    assert!(screen.contains("SOUND             LISTEN"), "{screen}");
    assert!(screen.contains("KEYBOARD          PRESS ANY KEY"), "{screen}");

    // Every key a person presses comes back with its code.
    machine.type_key(b'B');
    machine.run_frames(4);
    assert!(machine.screen_text().contains("KEY $42 B"), "{}", machine.screen_text());
}

#[test]
fn a_language_that_is_not_there_is_refused_by_name() {
    let error = roms::language(Model::Kestrel, "nonsense").expect_err("no such ROM");
    assert!(error.contains("no such ROM: nonsense"), "{error}");
    assert!(error.contains("basic"), "the message lists what there is: {error}");
}
