// SPDX-License-Identifier: MIT
//
// The Kestrel on its own: a window, a tube, and a 6502 running the ROMs.

#include <QApplication>
#include <QPixmap>

#include <cstring>

#include "timewarp/kestrel/window.hpp"

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    application.setApplicationName("Kestrel");

    timewarp::kestrel::KestrelWindow window;
    window.show();

    // `--shot <file>` boots the machine, lets the ROM finish its banner, and
    // writes the tube to a PNG. It is how the window is checked without a
    // person watching it.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--shot") == 0) {
            window.machine().runFrames(70);
            const QPixmap shot = window.grab();
            if (!shot.save(QString::fromLocal8Bit(argv[i + 1]))) {
                return 1;
            }
            return 0;
        }
    }
    return QApplication::exec();
}
