// SPDX-License-Identifier: MIT
//
// The HB-1's window: a tube, a keyboard, and the machine between them.
//
// The widget paints what the processor wrote into screen memory and nothing
// else. It has no idea that the ROM contains a BASIC: it clocks the machine at
// 1 MHz, hands it keystrokes, and draws forty by twenty-five cells in the
// colours the ROM chose. If the ROM stopped drawing, the window would be blank,
// which is the property that makes it a computer rather than a picture of one.

#pragma once

#include <QMainWindow>
#include <QWidget>

#include <memory>

#include "timewarp/machine.hpp"

class QTimer;
class QLabel;

namespace timewarp::hb1 {

/// The glass: 40 x 25 cells, the cursor, and the border.
class Hb1Tube final : public QWidget {
    Q_OBJECT

public:
    explicit Hb1Tube(timewarp::Machine& machine, QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    timewarp::Machine& machine_;
};

/// The machine's case: the tube, a status strip, and the keyboard.
class Hb1Window final : public QMainWindow {
    Q_OBJECT

public:
    explicit Hb1Window(QWidget* parent = nullptr);
    ~Hb1Window() override;

    [[nodiscard]] timewarp::Machine& machine() noexcept { return machine_; }

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void tick();
    void buildMenus();
    void applySpeed(timewarp::Machine::Speed speed);
    void applySocketRom(std::string_view id);
    void chooseCartridge();
    void mountDisk();
    void saveDisk();
    void mountTape();
    void saveTape();
    void saveSettings() const;
    void loadSettings();

    timewarp::Machine machine_{timewarp::Model::Hb1};
    Hb1Tube* tube_ = nullptr;
    QTimer* clock_ = nullptr;
    QLabel* status_ = nullptr;
    std::uint64_t framesRun_ = 0;
    QString diskPath_;
    QString tapePath_;
};

}  // namespace timewarp::hb1
