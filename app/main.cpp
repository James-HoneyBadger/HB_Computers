// SPDX-License-Identifier: MIT
//
// kestrel_computers -- the machines' window.
//
// One executable, two machines, because they are one emulator: `--model kestrel`
// powers on the 1982 machine, `--model kestrel16` the 1992 one. With no
// argument the 1982 machine comes up, which is the one with a ROM socket to fill.
//
//   kestrel_computers                    the Kestrel
//   kestrel_computers --model kestrel16   the Kestrel 16
//   kestrel_computers --shot tube.png     photograph the machine and exit

#include <QApplication>
#include <QPixmap>

#include <cstdio>
#include <cstring>
#include <string_view>

#include "kestrel/model1982/window.hpp"

namespace {

void usage() {
    std::printf(
        "usage: kestrel_computers [--model kestrel|kestrel16] [--shot FILE]\n"
        "\n"
        "  --model   which machine to power on (default kestrel)\n"
        "  --shot    save a PNG of the machine and exit, for CI\n");
}

}  // namespace

int main(int argc, char** argv) {
    kestrel::Model model = kestrel::Model::Kestrel;
    const char* shotPath = nullptr;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            const std::string_view name = argv[++i];
            if (name == "kestrel") {
                model = kestrel::Model::Kestrel;
            } else if (name == "kestrel16" || name == "kestrel-16") {
                model = kestrel::Model::Kestrel16;
            } else {
                std::fprintf(stderr, "kestrel_computers: no such model: %s\n",
                             argv[i]);
                return 2;
            }
        } else if (arg == "--shot" && i + 1 < argc) {
            shotPath = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "kestrel_computers: unknown option: %s\n", argv[i]);
            usage();
            return 2;
        }
    }

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("HoneyBadger Systems"));
    QCoreApplication::setApplicationName(QStringLiteral("Kestrel Computers"));

    kestrel::model1982::KestrelWindow window{model};
    window.show();

    if (shotPath != nullptr) {
        // A turn of the event loop so the machine has painted its first frame,
        // then a second of the ROM's own time to finish the banner.
        window.machine().runFrames(70);
        const QPixmap shot = window.grab();
        if (!shot.save(QString::fromLocal8Bit(shotPath))) {
            std::fprintf(stderr, "kestrel_computers: cannot write %s\n", shotPath);
            return 3;
        }
        return 0;
    }
    return QApplication::exec();
}
