// SPDX-License-Identifier: MIT
//
// The front door: what a machine opened from the launcher has on it.
//
// The two complaints these pin are the two a person can have on the first
// minute: a computer with no files on its disk, and a screen that does not take
// the keyboard. Both are about the launcher and the glass, not about the
// engines, which is why they live here rather than in the machine's own cases.

#include "retro/support.hpp"

#include "machine_launcher.hpp"
#include "timewarp/ui/app_paths.hpp"
#include "timewarp/ui/retro_computer.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QKeyEvent>

namespace tw::retro {

namespace {

/// A launcher on the heap, torn down the way the application tears it down: the
/// machine windows close themselves through `deleteLater`, so those deletions
/// are run before the launcher that owns them goes away.
struct LauncherHandle {
    hb::MachineLauncher* launcher = new hb::MachineLauncher;

    ~LauncherHandle() {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        delete launcher;
    }
};

}  // namespace

void test_launcher_installs_the_shipped_disk() {
    clearMachineSettings();
    const QString image = HbDos::defaultImagePath();
    TW_CHECK(!image.isEmpty());
    QFile::remove(image);

    LauncherHandle handle;
    auto* machine = handle.launcher->openHb1();
    TW_CHECK(machine != nullptr);
    if (machine == nullptr) {
        return;
    }
    // The repo ships nine programs for this machine; a first boot has to have
    // them, or `CATALOG` answers with an empty disk and there is nothing to load.
    TW_CHECK(machine->disk().files().size() >= 9);
    TW_CHECK(QFile::exists(image));
    TW_CHECK(machine->disk().find(QStringLiteral("TUBE WRITER")) != nullptr);
    machine->close();
}

void test_typing_at_the_glass_reaches_the_prompt() {
    clearMachineSettings();
    RetroComputerWindow machine;
    machine.show();
    QCoreApplication::processEvents();

    auto* tube = machine.consoleView();
    TW_CHECK(tube != nullptr);
    if (tube == nullptr) {
        return;
    }
    machine.promptInput()->clear();
    for (const char letter : {'P', 'R', 'I', 'N', 'T'}) {
        QKeyEvent press{QEvent::KeyPress, letter, Qt::NoModifier,
                        QString{QLatin1Char(letter)}};
        QCoreApplication::sendEvent(tube, &press);
    }
    TW_CHECK_EQ(machine.promptInput()->text(), QStringLiteral("PRINT"));
    machine.promptInput()->clear();
}

}  // namespace tw::retro
