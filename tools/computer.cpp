// SPDX-License-Identifier: MIT
//
// hb_computer -- one emulator, two machines.
//
// The same executable boots the HB-1 or the HB-2, because they are the same
// machine with different profiles. This is what a person sees: type at it, let it
// run, print the screen. Everything on that screen came out of the model's ROMs.

#include "timewarp/machine.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    timewarp::Model model = timewarp::Model::Hb1;
    int frames = 40;
    timewarp::Machine::Speed speed = timewarp::Machine::Speed::Turbo;
    std::vector<std::string> lines;
    bool screen = true;
};

void usage() {
    std::printf(
        "usage: hb_computer [--model hb1|hb2] [--frames N] [--speed 1|2|4|8|turbo]\n"
        "                   [--type \"LINE\"]... [--no-screen]\n"
        "\n"
        "  --model    which machine to power on (default hb1)\n"
        "  --frames   frames to run before the screen is printed (default 40)\n"
        "  --type     type a line at the prompt and let the machine answer; may\n"
        "             be given more than once\n"
        "  --no-screen  print nothing; useful only for timing\n");
}

bool parseSpeed(std::string_view text, timewarp::Machine::Speed& out) {
    if (text == "1") { out = timewarp::Machine::Speed::Mhz1; return true; }
    if (text == "2") { out = timewarp::Machine::Speed::Mhz2; return true; }
    if (text == "4") { out = timewarp::Machine::Speed::Mhz4; return true; }
    if (text == "8") { out = timewarp::Machine::Speed::Mhz8; return true; }
    if (text == "turbo") { out = timewarp::Machine::Speed::Turbo; return true; }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto next = [&]() -> std::string_view {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "hb_computer: %s needs a value\n", argv[i]);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--model") {
            const std::string_view name = next();
            if (name == "hb1" || name == "hb-1") {
                options.model = timewarp::Model::Hb1;
            } else if (name == "hb2" || name == "hb-2") {
                options.model = timewarp::Model::Hb2;
            } else {
                std::fprintf(stderr, "hb_computer: no such model: %.*s\n",
                             static_cast<int>(name.size()), name.data());
                return 2;
            }
        } else if (arg == "--frames") {
            options.frames = std::atoi(std::string{next()}.c_str());
        } else if (arg == "--speed") {
            if (!parseSpeed(next(), options.speed)) {
                std::fprintf(stderr, "hb_computer: speed must be 1, 2, 4, 8 or turbo\n");
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
            std::fprintf(stderr, "hb_computer: unknown option: %s\n", argv[i]);
            usage();
            return 2;
        }
    }

    timewarp::Machine machine{options.model};
    machine.setSpeed(options.speed);
    machine.reset();
    machine.runFrames(options.frames);

    for (const std::string& line : options.lines) {
        machine.typeLine(line);
        machine.runFrames(20);
    }

    if (options.screen) {
        std::printf("%s\n", machine.screenText().c_str());
    }
    return 0;
}
