// SPDX-License-Identifier: MIT
//
// `hb2`: the HB-2 without the window.
//
// Boot a machine, assemble and run a listing, and print the registers, the screen
// text and a hash of the framebuffer. This is the counterpart of the machine
// window for a shell, a CI job or a person reading the manual:
//
//     hb2 Examples/hb2/palette.s --ticks 4 --screen
//     hb2 --map
//     hb2 --tasks Examples/hb2/task_a.s Examples/hb2/task_b.s --ticks 6
//
// A program that runs, halts and leaves the screen empty is reported as *drew
// nothing* rather than as a pass, because a machine that runs and shows nothing
// is the failure this project treats as the bug.
//
// Qt-free on purpose: the machine is, and a harness for it should be reachable on
// a build host that has no Qt at all.

#include "timewarp/hb2/asm.hpp"
#include "timewarp/hb2/machine.hpp"
#include "timewarp/hb2/map.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using timewarp::hb2::Assembly;
using timewarp::hb2::CpuStatus;
using timewarp::hb2::Machine;
using timewarp::hb2::Video;

/// The file name without its directory or extension: the name a task is known by.
std::string stemOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) {
        name.erase(dot);
    }
    for (char& c : name) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return name;
}

std::optional<std::string> readListing(const std::string& path) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

/// Whether the machine drew anything: a pixel that is not the background in a
/// bitmap mode, or a cell that is not a space in the 1982 text mode.
bool drewSomething(Machine& machine) {
    if (machine.video().mode() == Video::Mode::Text) {
        for (const std::string& row : machine.video().screenText()) {
            if (!row.empty()) {
                return true;
            }
        }
        return false;
    }
    for (int y = 0; y < machine.video().height(); ++y) {
        for (int x = 0; x < machine.video().width(); ++x) {
            if (machine.video().pixel(x, y) != 0) {
                return true;
            }
        }
    }
    return false;
}

void printScreen(Machine& machine) {
    std::printf("  screen\n");
    for (const std::string& row : machine.video().screenText()) {
        if (row.empty()) {
            continue;
        }
        std::printf("    | %s\n", row.c_str());
    }
}

void printMap() {
    std::printf("%-12s %s\n", "region", "addresses");
    for (const std::string& row : timewarp::hb2::mapListing()) {
        std::printf("%s\n", row.c_str());
    }
}

/// HB-OS through the harness: several programs, a scheduler and a desktop. Every
/// task must have printed something and none may have died -- two programs making
/// progress at once is the claim, and a picture of one window is not evidence.
int runTasks(const std::vector<std::string>& tasks, int ticks, bool screen) {
    Machine machine;
    for (const std::string& name : tasks) {
        const std::optional<std::string> source = readListing(name);
        if (!source) {
            std::fprintf(stderr, "hb2: cannot read %s\n", name.c_str());
            return 1;
        }
        if (machine.spawn(stemOf(name), *source) == 0) {
            std::fprintf(stderr, "hb2: %s did not start: %s\n", name.c_str(),
                         machine.scheduler().lastError().c_str());
            return 1;
        }
    }
    machine.runFrames(ticks);

    std::printf("HB-OS: %llu frames, %zu processes\n",
                static_cast<unsigned long long>(machine.video().frames()),
                machine.scheduler().size());
    for (const std::string& row : machine.scheduler().status()) {
        std::printf("  %s\n", row.c_str());
    }
    bool progress = true;
    for (const timewarp::hb2::Task& task : machine.scheduler().tasks()) {
        const std::size_t lines = static_cast<std::size_t>(
            std::count(task.console.begin(), task.console.end(), '\n'));
        std::printf("  %s printed %zu line(s)\n", task.name.c_str(), lines);
        if (lines == 0 && task.failure.empty()) {
            progress = false;
        }
        if (!task.failure.empty()) {
            progress = false;
            std::fprintf(stderr, "hb2: %s died: %s\n", task.name.c_str(),
                         task.failure.c_str());
        }
    }
    std::printf("  desktop      %zu window(s), pointer at %d,%d\n",
                machine.desktop().count(), machine.desktop().pointerX(),
                machine.desktop().pointerY());
    std::printf("  framebuffer  %016llX\n",
                static_cast<unsigned long long>(machine.video().hash()));
    if (screen) {
        printScreen(machine);
    }
    if (!progress) {
        std::fprintf(stderr, "hb2: a process made no progress\n");
        return 1;
    }
    return 0;
}

int runProgram(const std::string& program, int ticks, bool screen) {
    const std::optional<std::string> source = readListing(program);
    if (!source) {
        std::fprintf(stderr, "hb2: cannot read %s\n", program.c_str());
        return 1;
    }

    Machine machine;
    const Assembly assembly = machine.load(*source);
    if (!assembly.ok) {
        std::fprintf(stderr, "hb2: %s did not assemble\n", program.c_str());
        for (const std::string& error : assembly.errors) {
            std::fprintf(stderr, "      %s\n", error.c_str());
        }
        return 1;
    }

    machine.runFrames(ticks);

    const bool halted = machine.status() == CpuStatus::Halted;
    const bool faulted = machine.status() == CpuStatus::Faulted;
    const bool drew = drewSomething(machine);

    std::printf("%s\n", program.c_str());
    std::printf("  origin       $%06X, %zu bytes, entry $%06X\n", assembly.origin,
                assembly.bytes.size(), assembly.entry);
    std::printf("  status       %s\n", machine.statusText().c_str());
    std::printf("  registers    R0=%04X R1=%04X R2=%04X R3=%04X\n",
                machine.cpu().registers().r[0], machine.cpu().registers().r[1],
                machine.cpu().registers().r[2], machine.cpu().registers().r[3]);
    std::printf("  cycles       %llu in %llu instructions\n",
                static_cast<unsigned long long>(machine.cpu().registers().cycles),
                static_cast<unsigned long long>(machine.cpu().registers().instructions));
    std::printf("  video        mode %d, %dx%d\n", static_cast<int>(machine.video().mode()),
                machine.video().width(), machine.video().height());
    std::printf("  framebuffer  %016llX\n",
                static_cast<unsigned long long>(machine.video().hash()));
    if (!machine.console().empty()) {
        std::printf("  printed      \"%s\"\n", machine.console().c_str());
    }
    if (screen) {
        printScreen(machine);
    }

    // The verdict. A fault is a failure, and so is a program that ran and left the
    // screen as it found it -- the bug this machine exists to catch.
    if (faulted) {
        std::fprintf(stderr, "hb2: %s faulted\n", program.c_str());
        return 1;
    }
    if (!halted) {
        std::fprintf(stderr, "hb2: %s was still running after %d frames\n", program.c_str(),
                     ticks);
        return 1;
    }
    if (!drew) {
        std::fprintf(stderr, "hb2: %s ran and drew nothing\n", program.c_str());
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> listings;
    int ticks = 4;
    bool screen = false;
    bool map = false;
    bool tasks = false;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--screen") {
            screen = true;
        } else if (argument == "--map") {
            map = true;
        } else if (argument == "--tasks") {
            tasks = true;
        } else if (argument == "--ticks" && index + 1 < argc) {
            ticks = std::atoi(argv[++index]);
        } else if (argument.rfind("--ticks=", 0) == 0) {
            ticks = std::atoi(argument.c_str() + 8);
        } else if (argument == "--help" || argument == "-h") {
            std::printf(
                "usage: hb2 [--ticks N] [--screen] [--map] [--tasks] listing...\n");
            return 0;
        } else if (argument.rfind("--", 0) == 0) {
            std::fprintf(stderr, "hb2: unknown argument %s\n", argument.c_str());
            return 2;
        } else {
            listings.push_back(argument);
        }
    }

    if (ticks <= 0) {
        ticks = 4;
    }

    if (tasks && listings.empty()) {
        std::fprintf(stderr, "hb2: --tasks needs at least one listing\n");
        return 2;
    }
    if (map) {
        printMap();
        if (listings.empty()) {
            return 0;
        }
    }
    if (listings.empty()) {
        std::fprintf(stderr, "hb2: name a listing, or ask for --map\n");
        return 2;
    }
    if (tasks) {
        return runTasks(listings, ticks, screen);
    }
    if (listings.size() > 1) {
        std::fprintf(stderr, "hb2: one listing at a time, or --tasks then several\n");
        return 2;
    }
    return runProgram(listings.front(), ticks, screen);
}
