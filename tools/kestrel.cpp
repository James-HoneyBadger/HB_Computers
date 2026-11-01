// SPDX-License-Identifier: MIT
//
// kestrel -- one emulator, two machines.
//
// The same executable boots the Kestrel or the Kestrel 16, because they are the same
// machine with different profiles. This is what a person sees: type at it, let it
// run, print the screen. Everything on that screen came out of the model's ROMs.

#include "kestrel/machine.hpp"
#include "kestrel/model1982/rom.hpp"

using kestrel::SpecialKey;

#include <cstdio>
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    kestrel::Model model = kestrel::Model::Kestrel;
    int frames = 40;
    kestrel::Machine::Speed speed = kestrel::Machine::Speed::Turbo;
    std::vector<std::string> lines;
    std::string rom;
    std::string tape;
    bool screen = true;
};

void usage() {
    std::printf(
        "usage: kestrel [--model kestrel|kestrel16] [--frames N] [--speed 1|2|4|8|turbo]\n"
        "                   [--type \"LINE\"]... [--no-screen]\n"
        "\n"
        "  --model    which machine to power on (default kestrel)\n"
        "  --frames   frames to run before the screen is printed (default 40)\n"
        "  --type     type a line at the prompt and let the machine answer; may\n"
        "             be given more than once\n"
        "  --file     type every line of this file, in order, before any --type\n"
        "             lines; this is how a program in samples/ is run\n"
        "  --tape     insert this cassette image (.tap); a blank one otherwise\n"
        "  --rom      which ROM to put in the 1982 machine's socket: basic, or\n"
        "             monitor for machine code (default basic)\n"
        "  --no-screen  print nothing; useful only for timing\n");
}

bool parseSpeed(std::string_view text, kestrel::Machine::Speed& out) {
    if (text == "1") { out = kestrel::Machine::Speed::Mhz1; return true; }
    if (text == "2") { out = kestrel::Machine::Speed::Mhz2; return true; }
    if (text == "4") { out = kestrel::Machine::Speed::Mhz4; return true; }
    if (text == "8") { out = kestrel::Machine::Speed::Mhz8; return true; }
    if (text == "turbo") { out = kestrel::Machine::Speed::Turbo; return true; }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto next = [&]() -> std::string_view {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "kestrel: %s needs a value\n", argv[i]);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--model") {
            const std::string_view name = next();
            if (name == "kestrel") {
                options.model = kestrel::Model::Kestrel;
            } else if (name == "kestrel16" || name == "kestrel-16") {
                options.model = kestrel::Model::Kestrel16;
            } else {
                std::fprintf(stderr, "kestrel: no such model: %.*s\n",
                             static_cast<int>(name.size()), name.data());
                return 2;
            }
        } else if (arg == "--frames") {
            options.frames = std::atoi(std::string{next()}.c_str());
        } else if (arg == "--speed") {
            if (!parseSpeed(next(), options.speed)) {
                std::fprintf(stderr, "kestrel: speed must be 1, 2, 4, 8 or turbo\n");
                return 2;
            }
        } else if (arg == "--tape") {
            options.tape = std::string{next()};
        } else if (arg == "--rom") {
            options.rom = std::string{next()};
        } else if (arg == "--type") {
            options.lines.emplace_back(next());
        } else if (arg == "--file") {
            const std::string name{next()};
            std::ifstream in{name};
            if (!in) {
                std::fprintf(stderr, "kestrel: cannot read %s\n", name.c_str());
                return 2;
            }
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.empty()) {
                    options.lines.push_back(line);
                }
            }
        } else if (arg == "--no-screen") {
            options.screen = false;
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "kestrel: unknown option: %s\n", argv[i]);
            usage();
            return 2;
        }
    }

    kestrel::Machine machine{options.model};
    machine.setSpeed(options.speed);
    if (!options.rom.empty()) {
        const auto* rom = kestrel::model1982::findRom(options.rom);
        if (rom == nullptr) {
            std::fprintf(stderr, "kestrel: no such ROM: %s\n", options.rom.c_str());
            for (const auto& one : kestrel::model1982::romLibrary()) {
                std::fprintf(stderr, "  %.*s -- %.*s\n", static_cast<int>(one.id.size()),
                             one.id.data(), static_cast<int>(one.description.size()),
                             one.description.data());
            }
            return 2;
        }
        if (machine.model() != kestrel::Model::Kestrel) {
            std::fprintf(stderr, "kestrel: the 1992 machine's language ROM is a bank, not a socket\n");
            return 2;
        }
        if (!machine.mountSocketRom(*rom->image, rom->id)) {
            std::fprintf(stderr, "kestrel: %s does not fit the socket\n", options.rom.c_str());
            return 2;
        }
    }
    machine.reset();
    if (!options.tape.empty()) {
        std::ifstream in(options.tape, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "kestrel: cannot read tape: %s\n", options.tape.c_str());
            return 2;
        }
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                             std::istreambuf_iterator<char>()};
        machine.tape().load(bytes);
    }
    machine.tape().insert();
    machine.runFrames(options.frames);

    // A person types at the speed of the emulated machine, not the host's: the
    // keyboard controller holds sixteen keys, and a line pasted in at once
    // would overflow it and lose characters -- including the Return, which
    // welds two lines together.
    const auto typeKey = [&machine](std::uint8_t key) {
        while (machine.keyboard().size() >= 8) {
            machine.runFrames(1);
        }
        machine.type(key);
        machine.runFrames(2);
    };
    for (const std::string& line : options.lines) {
        for (const char ch : line) {
            typeKey(static_cast<std::uint8_t>(ch));
        }
        typeKey(static_cast<std::uint8_t>(SpecialKey::Return));
        while (machine.keyboard().size() > 0) {
            machine.runFrames(1);
        }
        machine.runFrames(20);
    }
    // A program worth watching takes longer than the typing did.
    machine.runFrames(options.frames);

    if (options.screen) {
        std::printf("%s\n", machine.screenText().c_str());
    }
    return 0;
}
