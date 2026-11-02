// SPDX-License-Identifier: MIT
//
// Kestrel PILOT: the teaching language in the socket ROM. What the manual
// promises is that a numbered line can be written, listed, replaced and
// deleted, that RUN obeys the program, and that a lesson can ask a question and
// go somewhere on the answer. Those promises are the test.

use kestrel_machine::machine::Machine;
use kestrel_machine::profile::Model;

/// A Kestrel with PILOT in the socket, past its banner.
fn pilot() -> Machine {
    let mut machine = Machine::new(Model::Kestrel);
    machine.mount_assembled_roms().expect("the ROMs to assemble");
    machine
        .mount_assembled_language("pilot")
        .expect("the PILOT ROM to assemble");
    machine.reset();
    machine.run_frames(10);
    machine
}

fn type_lines(machine: &mut Machine, lines: &[&str]) {
    for line in lines {
        machine.type_line_paced(line);
    }
    machine.run_frames(10);
}

/// The tube's rows, trimmed, with the empty ones dropped.
fn rows(machine: &Machine) -> Vec<String> {
    machine
        .screen_text()
        .lines()
        .map(|row| row.trim().to_string())
        .filter(|row| !row.is_empty())
        .collect()
}

/// What the machine said, without the prompts and the lines it echoed back:
/// a lesson's own output is what a pupil reads.
fn said(machine: &Machine) -> Vec<String> {
    rows(machine)
        .into_iter()
        .filter(|row| !row.starts_with('>'))
        .collect()
}

fn said_text(machine: &Machine) -> String {
    said(machine).join("\n")
}

fn screen(machine: &Machine) -> String {
    machine.screen_text()
}

#[test]
fn the_language_boots_with_its_own_banner() {
    let screen = screen(&pilot());
    assert!(screen.contains("KESTREL PILOT V1.0"), "{screen}");
    assert!(screen.contains("T: A: M: Y: N: J: E: R:"), "{screen}");
}

#[test]
fn lines_are_listed_in_order_however_they_were_typed() {
    let mut machine = pilot();
    type_lines(
        &mut machine,
        &["30 T:THIRD", "10 T:FIRST", "20 T:SECOND", "LIST"],
    );
    let listing: Vec<String> = said(&machine)
        .into_iter()
        .filter(|row| row.contains(" T:"))
        .collect();
    assert_eq!(listing, ["10 T:FIRST", "20 T:SECOND", "30 T:THIRD"]);
}

#[test]
fn run_obeys_the_program_in_order() {
    let mut machine = pilot();
    type_lines(&mut machine, &["10 T:FIRST", "20 T:SECOND", "30 T:THIRD", "RUN"]);
    let spoken: Vec<String> = said(&machine)
        .into_iter()
        .filter(|row| ["FIRST", "SECOND", "THIRD"].contains(&row.as_str()))
        .collect();
    assert_eq!(spoken, ["FIRST", "SECOND", "THIRD"]);
}

#[test]
fn a_right_answer_takes_the_yes_branch() {
    let mut machine = pilot();
    type_lines(
        &mut machine,
        &[
            "10 T:WHAT IS THE CAPITAL OF FRANCE?",
            "20 A:C",
            "30 M:PARIS",
            "40 Y:RIGHT",
            "50 T:NO, IT IS PARIS.",
            "60 E:",
            "70 :RIGHT",
            "80 T:THAT IS RIGHT.",
            "90 E:",
            "RUN",
            "paris",
        ],
    );
    let screen = said_text(&machine);
    assert!(screen.contains("THAT IS RIGHT."), "{screen}");
    assert!(!screen.contains("NO, IT IS PARIS."), "{screen}");
}

#[test]
fn a_wrong_answer_takes_the_no_branch_and_the_answer_can_be_printed() {
    let mut machine = pilot();
    type_lines(
        &mut machine,
        &[
            "10 A:N",
            "20 M:ALAN",
            "30 Y:KNOWN",
            "40 T:WHO IS $N?",
            "50 E:",
            "60 :KNOWN",
            "70 T:HELLO $N.",
            "80 E:",
            "RUN",
            "Bob",
        ],
    );
    let screen = said_text(&machine);
    assert!(screen.contains("WHO IS Bob?"), "{screen}");
    assert!(!screen.contains("HELLO"), "{screen}");
}

#[test]
fn an_answer_is_matched_without_regard_to_case() {
    let mut machine = pilot();
    type_lines(
        &mut machine,
        &[
            "10 A:R",
            "20 M:CORRECT HORSE",
            "30 Y:FINE",
            "40 T:NO",
            "50 E:",
            "60 :FINE",
            "70 T:YES",
            "80 E:",
            "RUN",
            "correct horse",
        ],
    );
    let screen = said_text(&machine);
    assert!(said(&machine).contains(&"YES".to_string()), "{screen}");
    assert!(!said(&machine).contains(&"NO".to_string()), "{screen}");
}

#[test]
fn a_star_matches_whatever_was_typed() {
    let mut machine = pilot();
    type_lines(
        &mut machine,
        &[
            "10 A:R",
            "20 M:*",
            "30 Y:OK",
            "40 T:NOTHING",
            "50 E:",
            "60 :OK",
            "70 T:ANYTHING",
            "80 E:",
            "RUN",
            "xyzzy",
        ],
    );
    let screen = said_text(&machine);
    assert!(said(&machine).contains(&"ANYTHING".to_string()), "{screen}");
    assert!(!said(&machine).contains(&"NOTHING".to_string()), "{screen}");
}

#[test]
fn a_line_can_be_replaced_and_deleted() {
    let mut machine = pilot();
    type_lines(
        &mut machine,
        &["10 T:ONE", "20 T:TWO", "10 T:UNO", "20", "LIST"],
    );
    let listing: Vec<String> = said(&machine)
        .into_iter()
        .filter(|row| row.contains(" T:"))
        .collect();
    assert_eq!(listing, ["10 T:UNO"]);
}

#[test]
fn a_line_with_no_number_is_obeyed_at_once() {
    let mut machine = pilot();
    type_lines(&mut machine, &["T:TRIAL"]);
    assert!(
        said(&machine).contains(&"TRIAL".to_string()),
        "{}",
        screen(&machine)
    );
}

#[test]
fn a_jump_to_a_label_that_is_not_there_says_so_and_stops() {
    let mut machine = pilot();
    type_lines(&mut machine, &["10 J:NOWHERE", "20 T:NEVER", "RUN"]);
    let screen = said_text(&machine);
    assert!(screen.contains("no such label"), "{screen}");
    assert!(!said(&machine).contains(&"NEVER".to_string()), "{screen}");
}

#[test]
fn new_empties_the_program() {
    let mut machine = pilot();
    type_lines(&mut machine, &["10 T:GONE", "NEW", "LIST", "RUN"]);
    // The line was echoed as it was typed; a LIST or a RUN after NEW would put
    // it on the tube a second time.
    assert_eq!(
        screen(&machine).matches("10 T:GONE").count(),
        1,
        "NEW left the line behind:\n{}",
        screen(&machine)
    );
}

#[test]
fn a_remark_and_a_label_are_listed_but_say_nothing() {
    let mut machine = pilot();
    type_lines(
        &mut machine,
        &["5 R:A REMARK", "10 :START", "20 E:", "LIST", "RUN"],
    );
    let listing: Vec<String> = said(&machine)
        .into_iter()
        .filter(|row| row.starts_with('5') || row.starts_with("10 "))
        .collect();
    assert_eq!(listing, ["5 R:A REMARK", "10 :START"]);
    // Running them prints nothing at all: the last thing on the tube is the
    // prompt the program came back to.
    assert_eq!(rows(&machine).last().map(String::as_str), Some(">"));
}
