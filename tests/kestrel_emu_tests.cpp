// SPDX-License-Identifier: MIT
//
// kestrel_emu: the front end that chooses a machine.
//
// The machine itself is proved by the two machine suites; what is proved here is
// the program around it -- that `--model` powers on the machine it names, that a
// session can switch machines, and that a command line that is not a machine is
// refused. Each check runs the real executable and reads what it printed, which
// is the only way to test a program whose whole job is a terminal.

#include "check.hpp"

#include <cstdio>
#include <string>

namespace {

/// Runs a shell command and returns everything it printed.
std::string shell(const std::string& command) {
    std::string output;
    FILE* pipe = ::popen((command + " 2>&1").c_str(), "r");
    if (pipe == nullptr) {
        return output;
    }
    char buffer[4096];
    while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) {
        output += buffer;
    }
    ::pclose(pipe);
    return output;
}

std::string runEmulator(const std::string& arguments) {
    return shell("\"" + std::string{KESTREL_EMU_PATH} + "\" " + arguments +
                 " < /dev/null");
}

/// Feeds a scripted session to the emulator and returns the final screen.
std::string script(const std::string& arguments, const std::string& input) {
    std::string script;
    for (const char ch : input) {
        if (ch == '\n') {
            script += "\\n";
        } else {
            script += ch;
        }
    }
    return shell("printf '" + script + "' | \"" +
                 std::string{KESTREL_EMU_PATH} + "\" " + arguments);
}

bool shows(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

void test_the_model_flag_chooses_the_machine() {
    // The 1982 machine: a 6502 KERNAL's banner, and its own arithmetic.
    const std::string kestrel = runEmulator("--model kestrel --type \"PRINT 2+2\"");
    TW_CHECK(shows(kestrel, "KESTREL BASIC V2.0"));
    TW_CHECK(shows(kestrel, "64K RAM SYSTEM"));
    TW_CHECK(!shows(kestrel, "KESTREL 16 BASIC 2.0"));

    // The 1992 machine: the other banner, and sixteen-bit arithmetic that the
    // 1982 machine's BASIC could not hold.
    const std::string kestrel16 =
        runEmulator("--model kestrel16 --type \"PRINT 30000+1\"");
    TW_CHECK(shows(kestrel16, "KESTREL 16 BASIC 2.0"));
    TW_CHECK(shows(kestrel16, "30001"));
    TW_CHECK(!shows(kestrel16, "KESTREL BASIC V2.0"));

    // Both spellings of the 1992 name.
    TW_CHECK(shows(runEmulator("--model kestrel-16 --type \"PRINT 1\""),
                   "KESTREL 16 BASIC 2.0"));
}

void test_a_session_switches_machines() {
    // Power on the 1982 machine, then switch: the screen at the end is the 1992
    // machine's, and it answers a question only its arithmetic can.
    const std::string out =
        script("--model kestrel", "/model kestrel16\nPRINT -7/2\n/quit\n");
    TW_CHECK(shows(out, "KESTREL 16 BASIC 2.0"));
    TW_CHECK(shows(out, "-3"));
    TW_CHECK(shows(out, "Kestrel 16 powered on"));

    // And back again, which is a cold boot of the machine that was there before.
    const std::string back =
        script("--model kestrel16", "/model kestrel\nPRINT 2+2\n/quit\n");
    TW_CHECK(shows(back, "KESTREL BASIC V2.0"));
    TW_CHECK(shows(back, "Kestrel powered on"));
}

void test_the_commands_are_commands() {
    const std::string out = script("--model kestrel", "/status\n/wibble\n/quit\n");
    // The status line names the machine and its clock.
    TW_CHECK(shows(out, "Kestrel: 1000000 Hz"));
    // A command that is not one is named, and the session carries on.
    TW_CHECK(shows(out, "no such command: /wibble"));
}

void test_a_bad_model_is_refused() {
    // A machine that does not exist is a command-line error, not a guess.
    TW_CHECK(shows(runEmulator("--model wibble"), "no such model: wibble"));
    TW_CHECK(shows(runEmulator("--rom basic --model kestrel16"),
                   "is a bank, not a socket"));
}

void test_the_monitor_socket_still_works() {
    // The 1982 machine's socket is the one place a model flag and a ROM meet.
    const std::string out = runEmulator("--model kestrel --rom monitor");
    TW_CHECK(shows(out, "KESTREL MONITOR V1.0"));
}

}  // namespace

int main() {
    test_the_model_flag_chooses_the_machine();
    test_a_session_switches_machines();
    test_the_commands_are_commands();
    test_a_bad_model_is_refused();
    test_the_monitor_socket_still_works();
    return tw::test::summary("kestrel_emu_tests");
}
