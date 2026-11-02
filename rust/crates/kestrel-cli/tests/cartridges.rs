// SPDX-License-Identifier: MIT
//
// The cartridges: every one of them assembles into the image its slot takes, and
// the machine starts the one it is given instead of its language. A cartridge is
// the only part of either machine that was never in the ROM, so this is the test
// that the slot, the reset's scan for the four letters, and the sources in
// `cartridges/` all agree.

use kestrel_machine::machine::Machine;
use kestrel_machine::profile::Model;

fn boot_with(model: Model, cartridge: &str) -> Machine {
    let mut machine = Machine::new(model);
    machine
        .mount_assembled_roms()
        .expect("the ROM sources to assemble");
    machine
        .mount_assembled_cartridge(cartridge)
        .unwrap_or_else(|error| panic!("the {cartridge} cartridge: {error}"));
    machine.reset();
    machine.run_frames(40);
    machine
}

#[test]
fn every_cartridge_assembles_into_its_slot() {
    for model in [Model::Kestrel, Model::Kestrel16] {
        let names = kestrel_machine::roms::shipped_cartridges(model)
            .expect("a cartridge directory");
        assert!(!names.is_empty(), "no cartridges for {model:?}");
        let wanted = match model {
            Model::Kestrel => 4 * 1024,
            Model::Kestrel16 => 128 * 1024,
        };
        for name in names {
            let image = kestrel_machine::roms::cartridge(model, &name)
                .unwrap_or_else(|error| panic!("{name}: {error}"));
            assert_eq!(image.len(), wanted, "{name} does not fill the slot");
            assert_eq!(&image[0..4], b"KSTL", "{name} has no cartridge header");
        }
    }
}

/// The monitor is the one a 1982 machine was bought for: an address, the bytes
/// at it, and a way to change them.
#[test]
fn the_1982_monitor_dumps_and_changes_memory() {
    let mut machine = boot_with(Model::Kestrel, "monitor");
    assert!(
        machine.screen_text().contains("KESTREL MACHINE MONITOR"),
        "the cartridge's own banner:\n{}",
        machine.screen_text()
    );

    // The KERNAL's jump table is at $E000, and every entry is a JMP.
    machine.type_line_paced("D E000");
    machine.run_frames(60);
    assert!(
        machine.screen_text().contains("E000: 4C"),
        "a dump of the KERNAL's first row:\n{}",
        machine.screen_text()
    );

    machine.type_line_paced("M 1000 48 49 4A");
    machine.run_frames(60);
    machine.type_line_paced("D 1000");
    machine.run_frames(60);
    assert!(
        machine.screen_text().contains("1000: 48 49 4A"),
        "the bytes that were put there:\n{}",
        machine.screen_text()
    );

    // And back to the language the machine would have started.
    machine.type_line_paced("B");
    machine.run_frames(60);
    assert!(
        machine.screen_text().contains("KESTREL BASIC"),
        "the language after the monitor:\n{}",
        machine.screen_text()
    );
}

/// A cartridge that asks questions and marks them.
#[test]
fn the_1982_tutor_asks_a_table() {
    let mut machine = boot_with(Model::Kestrel, "tutor");
    machine.run_frames(120);
    let screen = machine.screen_text();
    assert!(screen.contains("KESTREL TUTOR"), "the banner:\n{screen}");
    assert!(
        screen.lines().any(|row| row.contains(" X ") && row.contains('=')),
        "a question to answer:\n{screen}"
    );
}

/// The 1992 machine's cartridge is started the same way, and its party piece is
/// the tube: four thousand cells, each with a colour of its own.
#[test]
fn the_1992_colour_cartridge_fills_the_tube() {
    let machine = boot_with(Model::Kestrel16, "colours");
    assert!(
        machine.screen_text().contains("KESTREL 16"),
        "the cartridge's own name on the tube:\n{}",
        machine.screen_text()
    );
    let colours: Vec<u8> = (0..4000)
        .map(|cell| machine.bus.video.attribute(cell))
        .collect();
    assert!(
        colours.iter().any(|colour| *colour != 0),
        "the cartridge left the tube its own colour"
    );
    assert!(
        colours.iter().collect::<std::collections::HashSet<_>>().len() > 4,
        "and more than one colour of it"
    );
}

/// An image that is not the whole slot is refused rather than half-fitted.
#[test]
fn a_cartridge_of_the_wrong_size_is_refused() {
    let path = std::env::temp_dir().join("kestrel-cartridge-size-test.bin");
    std::fs::write(&path, vec![0u8; 100]).expect("a short image");
    let mut machine = Machine::new(Model::Kestrel);
    let error = machine.mount_cartridge(&path).expect_err("a refusal");
    assert!(
        error.to_string().contains("4096"),
        "the slot's size is named: {error}"
    );
    assert!(machine.bus.cartridge.is_none(), "and nothing was fitted");
    let _ = std::fs::remove_file(&path);
}

/// With no cartridge in the slot the machine starts its language, which is what
/// the reset's scan has to leave alone.
#[test]
fn an_empty_slot_still_starts_the_language() {
    let mut machine = Machine::new(Model::Kestrel);
    machine.mount_assembled_roms().expect("the ROMs");
    machine.reset();
    machine.run_frames(40);
    assert!(machine.screen_text().contains("KESTREL BASIC"));

    let mut machine = Machine::new(Model::Kestrel16);
    machine.mount_assembled_roms().expect("the ROMs");
    machine.reset();
    machine.run_frames(40);
    assert!(machine.screen_text().contains("KESTREL 16 BASIC"));
}

/// The names `--cartridges` lists are the ones in the folder, for both machines.
#[test]
fn the_catalogue_names_the_sources() {
    let names = kestrel_machine::roms::shipped_cartridges(Model::Kestrel).expect("a catalogue");
    assert!(names.contains(&"monitor".to_string()), "{names:?}");
    assert!(names.contains(&"tutor".to_string()), "{names:?}");
    let names = kestrel_machine::roms::shipped_cartridges(Model::Kestrel16).expect("a catalogue");
    assert!(names.contains(&"colours".to_string()), "{names:?}");
}
