// SPDX-License-Identifier: MIT
//
// Kestrel Computers: the front door to the Kestrel and the Kestrel 16.
//
//   kestrel_computers              open the launcher
//   kestrel_computers --hb1        power on the Kestrel at once
//   kestrel_computers --machine IMG --hb1
//                             restore the Kestrel from a machine image
//   kestrel_computers --hb1 --screenshot hb1.png
//                             photograph the machine and exit, for CI

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTimer>
#include <QWidget>

#include <cstdio>

#include "machine_launcher.hpp"
#include "timewarp/ui/retro_computer.hpp"

namespace {

constexpr const char* kVersion = HB_VERSION;

}  // namespace

int main(int argc, char** argv) {
    QApplication app{argc, argv};
    QCoreApplication::setOrganizationName(QStringLiteral("HoneyBadger"));
    QCoreApplication::setApplicationName(QStringLiteral("Kestrel Computers"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(kVersion));
    QApplication::setApplicationDisplayName(QStringLiteral("Kestrel Computers"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "The Kestrel and the Kestrel 16: two complete home computers from a 1982 that "
        "never happened, and its 1992 successor."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption hb1_option{
        QStringLiteral("hb1"), QStringLiteral("Power on the Kestrel at once.")};
    const QCommandLineOption machine_option{
        QStringLiteral("machine"),
        QStringLiteral("Restore the Kestrel from a machine image."),
        QStringLiteral("image")};
    const QCommandLineOption screenshot_option{
        QStringLiteral("screenshot"),
        QStringLiteral("Save a PNG of the machine and exit."),
        QStringLiteral("file")};
    parser.addOption(hb1_option);
    parser.addOption(machine_option);
    parser.addOption(screenshot_option);
    parser.process(app);

    hb::MachineLauncher launcher;
    const bool machine_named =
        parser.isSet(hb1_option) || parser.isSet(machine_option);
    // A machine was asked for by name, so the front door stays shut: the window
    // that closes is then the machine's, and closing it ends the program.
    if (!machine_named || parser.isSet(screenshot_option)) {
        launcher.show();
    }

    if (parser.isSet(hb1_option) || parser.isSet(machine_option)) {
        launcher.openKestrel();
    }

    QString error;
    if (parser.isSet(machine_option) &&
        !launcher.openMachineImage(parser.value(machine_option), &error)) {
        std::fprintf(stderr, "kestrel_computers: %s\n", qPrintable(error));
        return 2;
    }

    if (const QString path = parser.value(screenshot_option); !path.isEmpty()) {
        QWidget* target = &launcher;
        if (auto* hb1 = launcher.findChild<timewarp::ui::RetroComputerWindow*>()) {
            target = hb1;
        }
        // One turn of the event loop so the machine has painted its first frame.
        QTimer::singleShot(1200, target, [target, path] {
            if (!target->grab().save(path)) {
                std::fprintf(stderr, "kestrel_computers: cannot write %s\n", qPrintable(path));
                QCoreApplication::exit(3);
                return;
            }
            QCoreApplication::quit();
        });
    }

    return QApplication::exec();
}
