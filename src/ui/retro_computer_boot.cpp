// SPDX-License-Identifier: MIT
//
// Power, boot, the self test and the frame tick.

#include "retro_computer_internal.hpp"

#include <algorithm>
#include <optional>

#include <QCloseEvent>
#include <QComboBox>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>

#include "timewarp/canvas/command_queue.hpp"
#include "timewarp/canvas/qt_bridge.hpp"
#include "timewarp/ui/code_editor.hpp"
#include "timewarp/ui/native_runner.hpp"
#include "timewarp/ui/crt_overlay.hpp"
#include "timewarp/ui/repl_runner.hpp"
#include "timewarp/ui/sound_player.hpp"
#include "timewarp/ui/turtle_canvas.hpp"

namespace timewarp::ui {

using namespace retro_detail;

void RetroComputerWindow::showProgramPage() {
    if (text_tabs_ != nullptr && program_ != nullptr) {
        text_tabs_->setCurrentWidget(program_);
    }
}

void RetroComputerWindow::loadProgram(const QString& source) {
    if (program_ == nullptr) {
        return;
    }
    program_->setPlainText(source);
    programs_[static_cast<int>(language_)] = source;
    program_->document()->setModified(false);
    updateStatus();
}

void RetroComputerWindow::coldBootSequence() {
    if (!powered_ || voice_ == nullptr) {
        return;
    }
    // A cold boot is a cold boot whether it came from the switch or from a ROM
    // change: the chirp, the self-test (when the start-up message is Full), the
    // banner. One function so the two paths cannot drift.
    voice_->play(MachineVoice::Event::Boot);
    if (settings_.banner == BootBanner::Full) {
        runSelfTest();
    }
    appendBootBanner();
}

void RetroComputerWindow::reboot() {
    stopRunner();
    stopRepl();
    if (tube_ != nullptr) {
        tube_->clear();
    }
    if (bridge_ != nullptr) {
        bridge_->drainNow();
    }
    if (canvas_ != nullptr) {
        canvas_->reset();
    }
    auto_shown_graphics_ = false;
    coldBootSequence();
    text_tabs_->setCurrentWidget(tube_);
    updateStatus();
}

void RetroComputerWindow::tickFrame() {
    if (!powered_) {
        return;
    }
    memory_.tickFrame();
    // The sound latch is a register, not a queue: a program pokes a code and the
    // machine plays it on the next frame, then clears the register itself so the
    // program does not have to.
    if (const int code = memory_.takeSoundLatch(); code != 0) {
        voice_->tone(code);
    }
}

bool RetroComputerWindow::runSelfTest() {
    if (!powered_) {
        return false;
    }
    // The machine checks the RAM it claims to have, before the banner says how
    // much there is. It says nothing when it passes: a 1982 machine that printed
    // a page of diagnostics before every prompt is not a 1982 machine.
    for (int address = 0; address < HbMemory::kUserRamSize; address += 257) {
        const auto pattern = static_cast<std::uint8_t>((address * 7 + 1) & 0xFF);
        if (!memory_.poke(address, pattern) || memory_.peek(address) != pattern) {
            appendConsole(QStringLiteral("?RAM FAILED AT %1").arg(address));
            return false;
        }
    }
    // The RAM goes back the way the machine found it: a check that leaves its
    // pattern behind is a check with a side effect.
    for (int address = 0; address < HbMemory::kUserRamSize; ++address) {
        memory_.poke(address, 0);
    }
    return true;
}

void RetroComputerWindow::setPowered(bool on) {
    if (powered_ == on) {
        return;
    }
    powered_ = on;
    if (tube_ != nullptr) {
        tube_->setPowered(on);
    }
    if (input_ != nullptr) {
        input_->setEnabled(on);
    }
    if (on && tube_ != nullptr) {
        tube_->setFocus(Qt::OtherFocusReason);
    }
    run_button_->setEnabled(on);
    stop_button_->setEnabled(on);
    if (config_button_ != nullptr) {
        config_button_->setEnabled(on);
    }
    if (lamp_label_ != nullptr) {
        lamp_label_->setProperty("powered", on);
        lamp_label_->style()->unpolish(lamp_label_);
        lamp_label_->style()->polish(lamp_label_);
    }
    if (!on) {
        // Off means off: the run stops, the clock stops, the tube goes dark and says
        // nothing. A machine that keeps computing with the switch off is a screenshot.
        stopRunner();
        stopRepl();
        clock_->stop();
        updateStatus();
        return;
    }
    clock_->start();
    // `reboot` is the cold boot, chirp included: playing one here as well made a
    // power-on sound twice.
    reboot();
}

void RetroComputerWindow::togglePower() { setPowered(!powered_); }

}  // namespace timewarp::ui
