// SPDX-License-Identifier: MIT
//
// kestrel -- one emulator, two machines.
//
// The same executable boots the Kestrel or the Kestrel 16, because they are the same
// machine with different profiles. This is what a person sees: type at it, let it
// run, print the screen. Everything on that screen came out of the model's ROMs.

#include "kestrel/machine.hpp"

using kestrel::SpecialKey;

#include <cstdio>
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
        } else if (arg == "--type") {
            options.lines.emplace_back(next());
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
    machine.reset();
    machine.runFrames(options.frames);

    // A person types at the speed of the emulated machine, not the host's: the
    // keyboard controller holds sixteen keys, and a line pasted in at once
    // would overflow it and lose characters.
    for (const std::string& line : options.lines) {
        for (const char ch : line) {
            machine.type(static_cast<std::uint8_t>(ch));
            machine.runFrames(2);
        }
        machine.type(static_cast<std::uint8_t>(SpecialKey::Return));
        machine.runFrames(20);
    }
    // A program worth watching takes longer than the typing did.
    machine.runFrames(options.frames);

    if (options.screen) {
        std::printf("%s\n", machine.screenText().c_str());
    }
    return 0;
}
