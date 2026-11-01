// SPDX-License-Identifier: MIT
//
// The launcher for the two machines.
//
// The HB-1 and the HB-2 are each a window of their own; this is only the front
// door. It owns no machine state: closing a machine ends it, and the next
// request builds a fresh one, because a live machine's RAM is the point and a
// dead window's is not.

#pragma once

#include <QMainWindow>
#include <QPointer>
#include <QString>

class QAction;

namespace timewarp::ui {
class RetroComputerWindow;
}  // namespace timewarp::ui

namespace hb {

class MachineLauncher final : public QMainWindow {
    Q_OBJECT

public:
    explicit MachineLauncher(QWidget* parent = nullptr);

    /// The HB-1, booted into its stored ROM.
    timewarp::ui::RetroComputerWindow* openHb1();

    /// The HB-1, restored from a machine image. `error` receives why it could
    /// not be opened, in the machine's own words.
    bool openMachineImage(const QString& path, QString* error);

private:
    void buildActions();
    /// Installs the shipped HB DOS volume the first time an HB-1 is opened, so a
    /// machine that has never saved anything still has a disk with programs on it.
    void installDemoDisk();

    QAction* hb1_action_ = nullptr;
    QPointer<timewarp::ui::RetroComputerWindow> hb1_;
};

}  // namespace hb
