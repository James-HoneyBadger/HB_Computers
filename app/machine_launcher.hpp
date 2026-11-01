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
class Hb2Window;
class RetroComputerWindow;
}  // namespace timewarp::ui

namespace hb {

class MachineLauncher final : public QMainWindow {
    Q_OBJECT

public:
    explicit MachineLauncher(QWidget* parent = nullptr);

    /// The HB-1, booted into its stored ROM.
    timewarp::ui::RetroComputerWindow* openHb1();

    /// The HB-2, with the desktop listing loaded.
    timewarp::ui::Hb2Window* openHb2();

    /// The HB-1, restored from a machine image. `error` receives why it could
    /// not be opened, in the machine's own words.
    bool openMachineImage(const QString& path, QString* error);

    /// The HB-2, with a listing read from `path`.
    bool openHb2Listing(const QString& path, QString* error);

private:
    void buildActions();

    QAction* hb1_action_ = nullptr;
    QAction* hb2_action_ = nullptr;
    QPointer<timewarp::ui::RetroComputerWindow> hb1_;
    QPointer<timewarp::ui::Hb2Window> hb2_;
};

}  // namespace hb
