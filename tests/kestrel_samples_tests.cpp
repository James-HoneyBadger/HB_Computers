// SPDX-License-Identifier: MIT
//
// The sample programs in samples/, run on the machines they were written for.
//
// A sample is a text file a person would type: the 1982 machine's files are
// BASIC programs that are stored and RUN, and the Kestrel 16's are the direct
// statements its language takes today. Each one has a .expected file beside it
// holding the lines the machine must put on the screen, which is also how the
// books show what a sample does.

#include "check.hpp"

#include "kestrel/machine.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

using kestrel::Machine;
using kestrel::Model;
using kestrel::SpecialKey;

std::vector<std::string> readLines(const fs::path& path) {
    std::vector<std::string> lines;
    std::ifstream in{path};
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

/// Types a line the way a person does, letting the machine run between keys:
/// the keyboard controller holds sixteen keys and a pasted line would overflow
/// it, losing the Return and welding two lines together.
void typeLine(Machine& machine, const std::string& text) {
    for (const char ch : text) {
        while (machine.keyboard().size() >= 8) {
            machine.runFrames(1);
        }
        machine.type(static_cast<std::uint8_t>(ch));
        machine.runFrames(2);
    }
    machine.type(static_cast<std::uint8_t>(SpecialKey::Return));
    while (machine.keyboard().size() > 0) {
        machine.runFrames(1);
    }
    machine.runFrames(20);
}

/// A line to look for on the screen. A leading `~` means "somewhere in the
/// screen text", for output that shares a row with the prompt.
bool onScreen(const Machine& machine, const std::string& wanted) {
    if (!wanted.empty() && wanted.front() == '~') {
        return machine.screenText().find(wanted.substr(1)) != std::string::npos;
    }
    for (int row = 0; row < machine.video().rows(); ++row) {
        if (machine.screenLine(row) == wanted) {
            return true;
        }
    }
    return false;
}

void runSample(const fs::path& sample) {
    const fs::path expected = sample.parent_path() / (sample.stem().string() + ".expected");
    const bool isKestrel16 = sample.parent_path().filename() == "kestrel16";

    Machine machine{isKestrel16 ? Model::Kestrel16 : Model::Kestrel};
    machine.runFrames(25);

    for (const std::string& line : readLines(sample)) {
        typeLine(machine, line);
    }
    if (!isKestrel16) {
        // The 1982 machine stores numbered lines; they have to be run.
        typeLine(machine, "RUN");
        machine.runFrames(400);
    } else {
        machine.runFrames(40);
    }

    const std::vector<std::string> wanted = readLines(expected);
    TW_CHECK(!wanted.empty());
    for (const std::string& line : wanted) {
        if (!onScreen(machine, line)) {
            TW_CHECK_EQ(std::string{"a line of the output"}, line);
            std::printf("     in sample: %s\n", sample.string().c_str());
        } else {
            TW_CHECK(true);
        }
    }
}

void test_every_sample(const fs::path& directory) {
    std::vector<fs::path> samples;
    for (const auto& entry : fs::directory_iterator{directory}) {
        if (entry.path().extension() == ".bas") {
            samples.push_back(entry.path());
        }
    }
    std::sort(samples.begin(), samples.end());
    TW_CHECK(!samples.empty());
    for (const fs::path& sample : samples) {
        runSample(sample);
    }
}

}  // namespace

int main() {
    const fs::path samples{KESTREL_SAMPLES_DIR};
    test_every_sample(samples / "kestrel");
    test_every_sample(samples / "kestrel16");
    return tw::test::summary("kestrel_samples_tests");
}
