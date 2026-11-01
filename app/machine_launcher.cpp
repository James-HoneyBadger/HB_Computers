// SPDX-License-Identifier: MIT

#include "machine_launcher.hpp"

#include <QAction>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QKeySequence>
#include <QLineEdit>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include "timewarp/ui/app_paths.hpp"
#include "timewarp/ui/retro_computer.hpp"

namespace hb {

namespace {

QLabel* paragraph(const QString& text) {
    auto* label = new QLabel{text};
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignHCenter);
    return label;
}

}  // namespace

MachineLauncher::MachineLauncher(QWidget* parent) : QMainWindow{parent} {
    setWindowTitle(QStringLiteral("Kestrel Computers"));
    resize(760, 480);

    auto* central = new QWidget{this};
    auto* layout = new QVBoxLayout{central};
    layout->setContentsMargins(48, 40, 48, 40);
    layout->setSpacing(18);

    auto* title = new QLabel{QStringLiteral("HB COMPUTERS")};
    QFont title_font = title->font();
    title_font.setPointSize(title_font.pointSize() + 18);
    title_font.setBold(true);
    title->setFont(title_font);
    title->setAlignment(Qt::AlignHCenter);
    layout->addWidget(title);

    layout->addWidget(paragraph(QStringLiteral(
        "Two computers from a 1982 that never happened, and its 1992 successor. "
        "Both are real machines: you can type at them, break them and come back "
        "to the same state.")));

    auto* hb1 = new QPushButton{QStringLiteral("Kestrel  \u00b7  1982")};
    hb1->setObjectName(QStringLiteral("hb1Button"));
    hb1->setMinimumHeight(56);
    connect(hb1, &QPushButton::clicked, this, [this] { openKestrel(); });
    layout->addWidget(hb1);
    layout->addWidget(paragraph(QStringLiteral(
        "Eleven language ROMs in one socket, a prompt, a 64K, a tube and a disk "
        "measured in 256-byte sectors.")));

    layout->addStretch(1);
    setCentralWidget(central);

    buildActions();
    statusBar()->showMessage(QStringLiteral("Choose a machine."));
}

void MachineLauncher::buildActions() {
    auto* menu = menuBar()->addMenu(QStringLiteral("&Machines"));

    hb1_action_ = menu->addAction(QStringLiteral("Kestrel Computer"));
    hb1_action_->setObjectName(QStringLiteral("retroComputerAction"));
    connect(hb1_action_, &QAction::triggered, this, [this] { openKestrel(); });

    menu->addSeparator();
    auto* quit = menu->addAction(QStringLiteral("&Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);
}

timewarp::ui::RetroComputerWindow* MachineLauncher::openKestrel() {
    if (hb1_ == nullptr) {
        installDemoDisk();
        auto* window = new timewarp::ui::RetroComputerWindow{this};
        window->setAttribute(Qt::WA_DeleteOnClose);
        hb1_ = window;
        connect(window, &QObject::destroyed, this, [this] { hb1_ = nullptr; });
    }
    hb1_->show();
    hb1_->raise();
    hb1_->activateWindow();
    // The prompt field is not on the machine's face; the glass is. Without this a
    // new window would leave the keyboard on the front panel, and the first thing
    // a person typed would change cartridge instead of reaching the machine.
    if (hb1_->consoleView() != nullptr) {
        hb1_->consoleView()->setFocus(Qt::OtherFocusReason);
    }
    statusBar()->showMessage(QStringLiteral("Kestrel powered on."));
    return hb1_;
}

void MachineLauncher::installDemoDisk() {
    const QString directory = timewarp::ui::dataDirFor(QStringLiteral("disks"));
    if (directory.isEmpty()) {
        return;
    }
    const QString target = directory + QStringLiteral("/HBWARP.dsk");
    if (QFileInfo::exists(target)) {
        return;
    }
    const QString examples = timewarp::ui::examplesDir();
    if (examples.isEmpty()) {
        return;
    }
    const QString source = examples + QStringLiteral("/hb1/HBWARP.dsk");
    if (!QFileInfo::exists(source)) {
        return;
    }
    if (QFile::copy(source, target)) {
        statusBar()->showMessage(tr("Kestrel DOS volume installed: %1").arg(target));
    }
}

bool MachineLauncher::openMachineImage(const QString& path, QString* error) {
    auto* machine = openKestrel();
    QString reason;
    if (!machine->loadMachine(path, &reason)) {
        if (error != nullptr) {
            *error = reason;
        }
        return false;
    }
    return true;
}

}  // namespace hb
