// SPDX-License-Identifier: MIT
//
// kestrel_emu -- an interactive emulator that runs either machine.
//
// One program, two models, chosen at the start or switched in the middle of a
// session. The machine itself is the `kestrel_machine` library -- the same one
// the window and the command line use -- so this file is only the hands: it
// powers a model on, types at it, lets it run, and shows the tube.
//
//   kestrel_emu                      asks which machine, when there is a keyboard
//   kestrel_emu --model kestrel16    powers on the 1992 machine
//   kestrel_emu --model kestrel --type "PRINT 2+2"   one line, then the screen
//
// Lines typed at the prompt go to the machine. A line beginning with `/` is a
// command for this program rather than the machine:
//
//   /help              this list
//   /model kestrel16   power on the other machine (a cold boot: memory is lost)
//   /speed 1|2|4|8|turbo
//   /frames N          frames to run after each line typed (default 20)
//   /run [N]           run N frames and draw what changed
//   /break             the BREAK key, a warm start
//   /rom basic|monitor which ROM is in the Kestrel's socket
//   /screen            draw the tube again
//   /status            the clock, the frame and the opcode count
//   /quit              leave
//
// The tube is drawn after anything is typed, without its blank tail, so a
// terminal session reads like a machine rather than a screenshot. A session whose
// input is a pipe -- a script -- draws when it asks with /screen and once at the
// end, because a screen after every line is noise when nobody is watching.

#include "kestrel/machine.hpp"
#include "kestrel/model1982/rom.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

using kestrel::Machine;
using kestrel::Model;
using kestrel::SpecialKey;

bool stdinIsTerminal() {
#if defined(_WIN32)
    return ::_isatty(::_fileno(stdin)) != 0;
#else
    return ::isatty(::fileno(stdin)) != 0;
#endif
}

const char* modelName(Model model) {
    return model == Model::Kestrel ? "Kestrel" : "Kestrel 16";
}

bool parseSpeed(std::string_view text, Machine::Speed& out) {
    if (text == "1") { out = Machine::Speed::Mhz1; return true; }
    if (text == "2") { out = Machine::Speed::Mhz2; return true; }
    if (text == "4") { out = Machine::Speed::Mhz4; return true; }
    if (text == "8") { out = Machine::Speed::Mhz8; return true; }
    if (text == "turbo") { out = Machine::Speed::Turbo; return true; }
    return false;
}

void usage() {
    std::printf(
        "usage: kestrel_emu [--model kestrel|kestrel16] [--speed 1|2|4|8|turbo]\n"
        "                   [--frames N] [--rom basic|monitor] [--type \"LINE\"]...\n"
        "                   [--file FILE]\n"
        "\n"
        "  --model    which machine to power on; asked for when there is a\n"
        "             keyboard and no flag (default kestrel)\n"
        "  --speed    how fast the host clocks it (default the machine's own)\n"
        "  --frames   frames to run after each line typed at the machine\n"
        "  --rom      the Kestrel's socket ROM: basic, or monitor\n"
        "  --type     type a line before the prompt appears; may be repeated\n"
        "  --file     type every line of a file, in order\n"
        "\n"
        "Inside a session, a line beginning with / is a command: /help lists them.\n");
}

/// One machine, with the pacing a person typing would meet.
///
/// The machine is held by pointer and replaced rather than assigned: its bus
/// refers to the machine that owns it, so a copy or an assignment would leave the
/// processor talking to the object that used to be there.
class Emulator {
public:
    explicit Emulator(Model model)
        : model_(model), machine_(std::make_unique<Machine>(model)) {}

    void setSpeed(Machine::Speed speed) {
        speed_ = speed;
        machine_->setSpeed(speed);
    }

    void setFrames(int frames) { frames_ = frames; }
    [[nodiscard]] int frames() const { return frames_; }

    /// A scripted session draws its screen when it asks to (/screen) and at the
    /// end, rather than after every line: thirty screens in a pipe is noise.
    void setQuiet(bool quiet) { quiet_ = quiet; }

    [[nodiscard]] Model model() const { return model_; }

    /// Power a machine on. A different model is a different machine, so this is a
    /// cold boot and anything in memory is lost; the same model is a reset.
    void selectModel(Model model) {
        model_ = model;
        machine_ = std::make_unique<Machine>(model);
        machine_->setSpeed(speed_);
        machine_->runFrames(20);
    }

    bool mountSocketRom(std::string_view id) {
        if (model_ != Model::Kestrel) {
            std::printf("kestrel_emu: the Kestrel 16's language ROM is a bank, "
                        "not a socket\n");
            return false;
        }
        const auto* rom = kestrel::model1982::findRom(id);
        if (rom == nullptr) {
            std::printf("kestrel_emu: no such ROM: %.*s\n",
                        static_cast<int>(id.size()), id.data());
            for (const auto& one : kestrel::model1982::romLibrary()) {
                std::printf("  %.*s -- %.*s\n", static_cast<int>(one.id.size()),
                            one.id.data(),
                            static_cast<int>(one.description.size()),
                            one.description.data());
            }
            return false;
        }
        if (!machine_->mountSocketRom(*rom->image, rom->id)) {
            std::printf("kestrel_emu: %.*s does not fit the socket\n",
                        static_cast<int>(id.size()), id.data());
            return false;
        }
        machine_->runFrames(20);
        return true;
    }

    /// Type one line at the machine and let it answer. The keys go in at the
    /// speed the machine could be typed at: the controller holds sixteen keys on
    /// the Kestrel and thirty-two on the Kestrel 16, and a line pasted in at once
    /// overflows it, losing characters -- the Return included.
    void typeLine(std::string_view text) {
        for (const char ch : text) {
            while (machine_->keyboard().size() >= 8) {
                machine_->runFrames(1);
            }
            machine_->type(static_cast<std::uint8_t>(ch));
            machine_->runFrames(2);
        }
        while (machine_->keyboard().size() >= 8) {
            machine_->runFrames(1);
        }
        machine_->type(static_cast<std::uint8_t>(SpecialKey::Return));

        // A machine in a loop of its own stops reading keys, and waiting for it
        // to start again would hang the terminal. Say so instead, and leave the
        // BREAK key to the person.
        int waited = 0;
        while (machine_->keyboard().size() > 0) {
            machine_->runFrames(1);
            if (++waited > 500) {
                std::printf("kestrel_emu: the machine is not reading its keyboard; "
                            "it may be looping (/break interrupts it)\n");
                break;
            }
        }
        machine_->runFrames(frames_);
    }

    void run(int frames) { machine_->runFrames(frames); }

    void pressBreak() {
        machine_->pressBreak();
        machine_->runFrames(20);
    }

    /// The tube, without the blank tail, which is what a terminal can show.
    /// `forced` is for /screen and for the last screen of a session.
    void draw(bool forced = false) const {
        if (quiet_ && !forced) {
            return;
        }
        int last = -1;
        for (int row = 0; row < machine_->video().rows(); ++row) {
            if (!machine_->screenLine(row).empty()) {
                last = row;
            }
        }
        std::printf("+");
        for (int column = 0; column < machine_->video().columns(); ++column) {
            std::printf("-");
        }
        std::printf("+\n");
        for (int row = 0; row <= last; ++row) {
            std::printf("|%s|\n", machine_->screenLine(row).c_str());
        }
        std::printf("+");
        for (int column = 0; column < machine_->video().columns(); ++column) {
            std::printf("-");
        }
        std::printf("+\n");
    }

    void status() const {
        std::printf("%s: %d Hz, frame %u, ", modelName(model_),
                    machine_->profile().clockHz,
                    static_cast<unsigned>(machine_->frameCounter()));
        if (model_ == Model::Kestrel) {
            std::printf("%llu cycles, %llu opcodes\n",
                        static_cast<unsigned long long>(
                            machine_->cpu6502().cycleCount()),
                        static_cast<unsigned long long>(
                            machine_->cpu6502().instructionCount()));
        } else {
            std::printf("%llu cycles\n",
                        static_cast<unsigned long long>(
                            machine_->cpu65816().cycleCount()));
        }
    }

private:
    Model model_;
    std::unique_ptr<Machine> machine_;
    Machine::Speed speed_ = Machine::Speed::Mhz1;
    int frames_ = 20;
    bool quiet_ = false;
};

/// True when the line was a command and has been dealt with.
bool command(Emulator& emulator, const std::string& line, bool& quit) {
    if (line.empty() || line.front() != '/') {
        return false;
    }
    std::string_view rest{line};
    rest.remove_prefix(1);
    const auto split = [&rest](std::string_view& head, std::string_view& tail) {
        const auto space = rest.find(' ');
        head = rest.substr(0, space);
        tail = space == std::string_view::npos ? std::string_view{}
                                               : rest.substr(space + 1);
    };
    std::string_view name;
    std::string_view argument;
    split(name, argument);

    if (name == "quit" || name == "q") {
        quit = true;
    } else if (name == "help" || name == "h") {
        std::printf(
            "  /model kestrel|kestrel16   power on that machine (cold boot)\n"
            "  /speed 1|2|4|8|turbo      how fast the host clocks it\n"
            "  /frames N                 frames run after each line (now %d)\n"
            "  /run [N]                  run N frames\n"
            "  /break                    the BREAK key\n"
            "  /rom basic|monitor        the Kestrel's socket ROM\n"
            "  /screen                   draw the tube\n"
            "  /status                   clock, frame, cycles\n"
            "  /quit                     leave\n"
            "anything else is typed at the machine\n",
            emulator.frames());
    } else if (name == "model") {
        if (argument == "kestrel" || argument == "kestrel16" ||
            argument == "kestrel-16") {
            const Model wanted = argument == "kestrel" ? Model::Kestrel
                                                       : Model::Kestrel16;
            const bool different = wanted != emulator.model();
            emulator.selectModel(wanted);
            std::printf("kestrel_emu: %s %s\n", modelName(wanted),
                        different ? "powered on" : "cold booted");
            emulator.draw();
        } else {
            std::printf("kestrel_emu: /model wants kestrel or kestrel16\n");
        }
    } else if (name == "speed") {
        Machine::Speed speed = Machine::Speed::Mhz1;
        if (parseSpeed(argument, speed)) {
            emulator.setSpeed(speed);
            if (argument == "turbo") {
                std::printf("kestrel_emu: as fast as this computer\n");
            } else {
                std::printf("kestrel_emu: %.*s MHz\n",
                            static_cast<int>(argument.size()), argument.data());
            }
        } else {
            std::printf("kestrel_emu: /speed wants 1, 2, 4, 8 or turbo\n");
        }
    } else if (name == "frames") {
        const int frames = std::atoi(std::string{argument}.c_str());
        if (frames > 0) {
            emulator.setFrames(frames);
            std::printf("kestrel_emu: %d frames a line\n", frames);
        } else {
            std::printf("kestrel_emu: /frames wants a number above zero\n");
        }
    } else if (name == "run") {
        const int frames = argument.empty()
                               ? 50
                               : std::atoi(std::string{argument}.c_str());
        emulator.run(frames > 0 ? frames : 50);
        emulator.draw();
    } else if (name == "break") {
        emulator.pressBreak();
        emulator.draw();
    } else if (name == "rom") {
        if (emulator.mountSocketRom(argument)) {
            emulator.draw();
        }
    } else if (name == "screen") {
        emulator.draw(true);
    } else if (name == "status") {
        emulator.status();
    } else {
        std::printf("kestrel_emu: no such command: /%.*s (try /help)\n",
                    static_cast<int>(name.size()), name.data());
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Model model = Model::Kestrel;
    bool modelChosen = false;
    Machine::Speed speed = Machine::Speed::Mhz1;
    int frames = 20;
    std::string rom;
    std::vector<std::string> lines;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto next = [&]() -> std::string_view {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "kestrel_emu: %s needs a value\n", argv[i]);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--model") {
            const std::string_view name = next();
            if (name == "kestrel") {
                model = Model::Kestrel;
            } else if (name == "kestrel16" || name == "kestrel-16") {
                model = Model::Kestrel16;
            } else {
                std::fprintf(stderr, "kestrel_emu: no such model: %.*s\n",
                             static_cast<int>(name.size()), name.data());
                return 2;
            }
            modelChosen = true;
        } else if (arg == "--speed") {
            if (!parseSpeed(next(), speed)) {
                std::fprintf(stderr, "kestrel_emu: speed must be 1, 2, 4, 8 or turbo\n");
                return 2;
            }
        } else if (arg == "--frames") {
            frames = std::atoi(std::string{next()}.c_str());
        } else if (arg == "--rom") {
            rom = std::string{next()};
        } else if (arg == "--type") {
            lines.emplace_back(next());
        } else if (arg == "--file") {
            const std::string name{next()};
            std::ifstream in{name};
            if (!in) {
                std::fprintf(stderr, "kestrel_emu: cannot read %s\n", name.c_str());
                return 2;
            }
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.empty()) {
                    lines.push_back(line);
                }
            }
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "kestrel_emu: unknown option: %s\n", argv[i]);
            usage();
            return 2;
        }
    }

    const bool interactive = stdinIsTerminal();
    if (!modelChosen && interactive) {
        std::printf("Which machine?\n  1  Kestrel      (6502, 1982)\n"
                    "  2  Kestrel 16   (65816, 1992)\n> ");
        std::fflush(stdout);
        std::string answer;
        if (std::getline(std::cin, answer)) {
            // Anything that says 2 or 16 is the 1992 machine; anything else, and
            // a bare Return, is the 1982 one.
            if (!answer.empty() &&
                (answer.front() == '2' || answer.find("16") != std::string::npos)) {
                model = Model::Kestrel16;
            }
        }
    }

    Emulator emulator{model};
    emulator.setSpeed(speed);
    emulator.setFrames(frames > 0 ? frames : 20);
    if (!rom.empty() && !emulator.mountSocketRom(rom)) {
        return 2;
    }
    emulator.run(20);

    for (const std::string& line : lines) {
        emulator.typeLine(line);
    }

    if (!interactive) {
        // Nothing to prompt: read whatever commands and lines arrive on stdin,
        // then show the machine. This is what makes the emulator scriptable, and
        // it is why a script's screens are the ones it asks for and the last one.
        emulator.setQuiet(true);
        std::string line;
        bool quit = false;
        while (!quit && std::getline(std::cin, line)) {
            if (line.empty()) {
                continue;
            }
            if (!command(emulator, line, quit)) {
                emulator.typeLine(line);
            }
        }
        emulator.draw(true);
        return 0;
    }

    std::printf("%s powered on. /help lists the commands; a line without a\n"
                "leading / is typed at the machine.\n",
                modelName(emulator.model()));
    emulator.draw();

    std::string line;
    bool quit = false;
    while (!quit) {
        std::printf("/> ");
        std::fflush(stdout);
        if (!std::getline(std::cin, line)) {
            break;
        }
        if (line.empty()) {
            continue;
        }
        if (command(emulator, line, quit)) {
            continue;
        }
        emulator.typeLine(line);
        emulator.draw();
    }
    std::printf("kestrel_emu: %s off.\n", modelName(emulator.model()));
    return 0;
}
