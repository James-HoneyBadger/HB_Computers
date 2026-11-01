// SPDX-License-Identifier: MIT

#include "timewarp/hb1/window.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>

#include <fstream>

#include "timewarp/hb1/rom.hpp"

#include <array>

namespace timewarp::hb1 {

namespace {

constexpr int kCellWidth = 16;
constexpr int kCellHeight = 24;
constexpr int kBorderPixels = 12;

/// The machine's sixteen colours, as the tube shows them.
const std::array<QColor, 16>& palette() {
    static const std::array<QColor, 16> colours = {
        QColor(0x00, 0x00, 0x00),   // black
        QColor(0xFF, 0xFF, 0xFF),   // white
        QColor(0x9F, 0x3B, 0x3B),   // red
        QColor(0x6A, 0xB6, 0xC4),   // cyan
        QColor(0x9C, 0x5A, 0xC4),   // purple
        QColor(0x4E, 0x9E, 0x4E),   // green
        QColor(0x3B, 0x4E, 0xB6),   // blue
        QColor(0xC4, 0xC4, 0x4E),   // yellow
        QColor(0xB6, 0x7A, 0x3B),   // orange
        QColor(0x8B, 0x5A, 0x2B),   // brown
        QColor(0xD9, 0x7A, 0x7A),   // light red
        QColor(0x5A, 0x5A, 0x5A),   // dark grey
        QColor(0x9E, 0x9E, 0x9E),   // grey
        QColor(0x9E, 0xD9, 0x9E),   // light green
        QColor(0x7A, 0x8B, 0xD9),   // light blue
        QColor(0xC4, 0xC4, 0xC4),   // light grey
    };
    return colours;
}

QColor colourOf(std::uint8_t code) {
    return palette()[code & 0x0F];
}

}  // namespace

Hb1Tube::Hb1Tube(timewarp::Machine& machine, QWidget* parent)
    : QWidget(parent), machine_(machine) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
}

QSize Hb1Tube::sizeHint() const {
    return {machine_.video().columns() * kCellWidth + 2 * kBorderPixels,
            machine_.video().rows() * kCellHeight + 2 * kBorderPixels};
}

QSize Hb1Tube::minimumSizeHint() const {
    return {machine_.video().columns() * 8 + 2 * kBorderPixels,
            machine_.video().rows() * 12 + 2 * kBorderPixels};
}

void Hb1Tube::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), colourOf(machine_.border()));

    // The cells scale to the widget: a bigger window is a bigger tube, the same
    // forty columns.
    const int usableWidth = width() - 2 * kBorderPixels;
    const int usableHeight = height() - 2 * kBorderPixels;
    const qreal cellWidth = static_cast<qreal>(usableWidth) / machine_.video().columns();
    const qreal cellHeight = static_cast<qreal>(usableHeight) / machine_.video().rows();
    const QRectF glass(kBorderPixels, kBorderPixels, usableWidth, usableHeight);

    painter.setRenderHint(QPainter::TextAntialiasing, false);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(static_cast<int>(cellHeight * 0.82));
    painter.setFont(font);

    const std::uint8_t paper = machine_.paper();
    painter.fillRect(glass, colourOf(paper));

    for (int row = 0; row < machine_.video().rows(); ++row) {
        for (int column = 0; column < machine_.video().columns(); ++column) {
            const int index = row * machine_.video().columns() + column;
            const std::uint8_t code = machine_.video().cell(index);
            const std::uint8_t attribute = machine_.video().attribute(index);
            const std::uint8_t ink = static_cast<std::uint8_t>(attribute >> 4);
            const std::uint8_t cellPaper = static_cast<std::uint8_t>(attribute & 0x0F);
            const QRectF cell(kBorderPixels + column * cellWidth,
                              kBorderPixels + row * cellHeight, cellWidth,
                              cellHeight);
            if (cellPaper != paper) {
                painter.fillRect(cell, colourOf(cellPaper));
            }
            if (code != ' ' && code != 0) {
                painter.setPen(colourOf(ink));
                painter.drawText(cell, Qt::AlignCenter,
                                 QString(QChar(static_cast<char>(code))));
            }
        }
    }

    // The cursor is the machine's, not the window's: the KERNAL lights and
    // blinks it, and this paints what it finds.
    if (machine_.cursorOn()) {
        const QRectF cell(kBorderPixels + machine_.cursorColumn() * cellWidth,
                          kBorderPixels + machine_.cursorRow() * cellHeight,
                          cellWidth, cellHeight);
        painter.fillRect(cell, colourOf(machine_.ink()));
        const int index =
            machine_.cursorRow() * machine_.video().columns() + machine_.cursorColumn();
        const std::uint8_t code = machine_.video().cell(index);
        if (code != ' ' && code != 0) {
            painter.setPen(colourOf(machine_.paper()));
            painter.drawText(cell, Qt::AlignCenter,
                             QString(QChar(static_cast<char>(code))));
        }
    }
}

Hb1Window::Hb1Window(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("HB-1  ·  6502 @ 1 MHz  ·  64K"));
    tube_ = new Hb1Tube(machine_, this);
    setCentralWidget(tube_);

    status_ = new QLabel(this);
    statusBar()->addPermanentWidget(status_);
    statusBar()->showMessage(
        tr("Type at the machine.  Ctrl+Q leaves the machine on."));

    machine_.onSound = [](std::uint8_t) { QApplication::beep(); };

    // Fifty frames a second, 20 000 cycles each: the machine's clock is the
    // window's clock, and it keeps running whether or not a key is pressed.
    clock_ = new QTimer(this);
    clock_->setTimerType(Qt::PreciseTimer);
    connect(clock_, &QTimer::timeout, this, &Hb1Window::tick);
    clock_->start(20);

    buildMenus();
    loadSettings();

    tube_->setFocus();
    resize(tube_->sizeHint());
}

// ---------------------------------------------------------------------------
// The front panel
// ---------------------------------------------------------------------------
//
// Everything a 1982 owner could change by reaching round the back, and a few
// things only an emulator can offer: the clock, which ROM is in the socket,
// what is in the cartridge slot, and what is in the drive and the cassette.

void Hb1Window::buildMenus() {
    QMenu* machine = menuBar()->addMenu(tr("&Machine"));

    QMenu* speed = machine->addMenu(tr("&Speed"));
    auto* speeds = new QActionGroup(this);
    const struct {
        const char* label;
        timewarp::Machine::Speed speed;
    } speedChoices[] = {
        {"1 MHz (1982)", timewarp::Machine::Speed::Mhz1},
        {"2 MHz", timewarp::Machine::Speed::Mhz2},
        {"4 MHz", timewarp::Machine::Speed::Mhz4},
        {"As fast as this computer", timewarp::Machine::Speed::Turbo},
    };
    for (const auto& choice : speedChoices) {
        QAction* action = speed->addAction(tr(choice.label));
        action->setCheckable(true);
        action->setChecked(machine_.speed() == choice.speed);
        speeds->addAction(action);
        const timewarp::Machine::Speed value = choice.speed;
        connect(action, &QAction::triggered, this, [this, value] { applySpeed(value); });
    }

    QMenu* socket = machine->addMenu(tr("&ROM socket"));
    auto* sockets = new QActionGroup(this);
    for (const RomInfo& rom : romLibrary()) {
        QAction* action = socket->addAction(QString::fromUtf8(rom.name.data(),
                                                             rom.name.size()));
        action->setCheckable(true);
        action->setChecked(machine_.socketRomId() == rom.id);
        action->setToolTip(QString::fromUtf8(rom.description.data(),
                                             rom.description.size()));
        sockets->addAction(action);
        const std::string id{rom.id};
        connect(action, &QAction::triggered, this, [this, id] { applySocketRom(id); });
    }

    QMenu* cartridge = machine->addMenu(tr("&Cartridge"));
    QAction* fit = cartridge->addAction(tr("&Fit 4K image..."));
    connect(fit, &QAction::triggered, this, &Hb1Window::chooseCartridge);
    QAction* eject = cartridge->addAction(tr("&Eject"));
    connect(eject, &QAction::triggered, this, [this] { machine_.ejectCartridge(); });

    machine->addSeparator();
    QAction* sound = machine->addAction(tr("&Sound"));
    sound->setCheckable(true);
    sound->setChecked(machine_.soundEnabled());
    connect(sound, &QAction::toggled, this,
            [this](bool on) { machine_.setSoundEnabled(on); });

    QMenu* media = menuBar()->addMenu(tr("&Media"));
    QAction* diskMount = media->addAction(tr("Mount &disk image..."));
    connect(diskMount, &QAction::triggered, this, &Hb1Window::mountDisk);
    QAction* diskSave = media->addAction(tr("&Save disk image..."));
    connect(diskSave, &QAction::triggered, this, &Hb1Window::saveDisk);
    QAction* diskFormat = media->addAction(tr("&Format disk"));
    connect(diskFormat, &QAction::triggered, this, [this] {
        machine_.disk().format();
        statusBar()->showMessage(tr("Disk formatted, volume HBWARP"), 4000);
    });
    media->addSeparator();
    QAction* tapeMount = media->addAction(tr("Insert &tape..."));
    connect(tapeMount, &QAction::triggered, this, &Hb1Window::mountTape);
    QAction* tapeSave = media->addAction(tr("Save &tape..."));
    connect(tapeSave, &QAction::triggered, this, &Hb1Window::saveTape);
    QAction* tapeEject = media->addAction(tr("&Eject tape"));
    connect(tapeEject, &QAction::triggered, this, [this] {
        machine_.tape().eject();
        tapePath_.clear();
    });

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    QAction* commands = help->addAction(tr("&Commands"));
    connect(commands, &QAction::triggered, this, [this] {
        machine_.typeLine("HELP");
        tube_->setFocus();
    });
}

void Hb1Window::applySpeed(timewarp::Machine::Speed speed) {
    machine_.setSpeed(speed);
    const int hz = machine_.cyclesPerSecond();
    statusBar()->showMessage(
        hz == 0 ? tr("Clock: as fast as this computer allows")
                : tr("Clock: %1 MHz").arg(hz / 1'000'000.0, 0, 'g', 3),
        4000);
    saveSettings();
}

void Hb1Window::applySocketRom(std::string_view id) {
    const RomInfo* rom = findRom(id);
    if (rom == nullptr) {
        return;
    }
    if (!machine_.mountSocketRom(*rom->image, rom->id)) {
        QMessageBox::warning(this, tr("HB-1"), tr("That image is the wrong size."));
        return;
    }
    statusBar()->showMessage(
        tr("%1 fitted; the machine cold-boots into it").arg(
            QString::fromUtf8(rom->name.data(), rom->name.size())),
        4000);
    saveSettings();
}

void Hb1Window::chooseCartridge() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Fit a cartridge"), QString(), tr("ROM images (*.rom *.bin);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    std::ifstream in(path.toStdString(), std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                          std::istreambuf_iterator<char>()};
    if (!machine_.mountCartridge(bytes, QFileInfo(path).baseName().toStdString())) {
        QMessageBox::warning(this, tr("HB-1"),
                             tr("A cartridge is 4096 bytes; that file is not."));
        return;
    }
    statusBar()->showMessage(tr("Cartridge fitted at $9000"), 4000);
}

void Hb1Window::mountDisk() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Mount a disk image"), QString(), tr("HB disk images (*.dsk);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    std::ifstream in(path.toStdString(), std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                          std::istreambuf_iterator<char>()};
    if (!machine_.disk().load(bytes)) {
        QMessageBox::warning(this, tr("HB-1"),
                             tr("An HB disk is 512 sectors of 256 bytes: 131072 bytes."));
        return;
    }
    diskPath_ = path;
    statusBar()->showMessage(
        tr("Disk mounted: %1").arg(machine_.disk().volumeName().c_str()), 4000);
    saveSettings();
}

void Hb1Window::saveDisk() {
    QString path = diskPath_;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, tr("Save the disk image"), "hbwarp.dsk",
                                            tr("HB disk images (*.dsk)"));
    }
    if (path.isEmpty()) {
        return;
    }
    std::ofstream out(path.toStdString(), std::ios::binary);
    const auto& bytes = machine_.disk().bytes();
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    diskPath_ = path;
    statusBar()->showMessage(tr("Disk written to %1").arg(path), 4000);
    saveSettings();
}

void Hb1Window::mountTape() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Insert a cassette"), QString(), tr("HB tape images (*.tap);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    std::ifstream in(path.toStdString(), std::ios::binary);
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),
                                          std::istreambuf_iterator<char>()};
    machine_.tape().load(bytes);
    machine_.tape().insert();
    tapePath_ = path;
    statusBar()->showMessage(tr("Tape inserted: %1 bytes").arg(bytes.size()), 4000);
    saveSettings();
}

void Hb1Window::saveTape() {
    QString path = tapePath_;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, tr("Save the cassette"), "hb1.tap",
                                            tr("HB tape images (*.tap)"));
    }
    if (path.isEmpty()) {
        return;
    }
    std::ofstream out(path.toStdString(), std::ios::binary);
    const auto& bytes = machine_.tape().bytes();
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    tapePath_ = path;
    statusBar()->showMessage(tr("Tape written to %1").arg(path), 4000);
    saveSettings();
}

void Hb1Window::saveSettings() const {
    QSettings settings;
    settings.setValue("machine/speed", static_cast<int>(machine_.speed()));
    settings.setValue("machine/rom", QString::fromStdString(std::string(machine_.socketRomId())));
    settings.setValue("machine/sound", machine_.soundEnabled());
    settings.setValue("media/disk", diskPath_);
    settings.setValue("media/tape", tapePath_);
}

void Hb1Window::loadSettings() {
    QSettings settings;
    machine_.setSpeed(static_cast<timewarp::Machine::Speed>(
        settings.value("machine/speed", static_cast<int>(timewarp::Machine::Speed::Mhz1)).toInt()));
    machine_.setSoundEnabled(settings.value("machine/sound", true).toBool());
    const QString rom = settings.value("machine/rom", "basic").toString();
    if (const RomInfo* found = findRom(rom.toStdString()); found != nullptr) {
        machine_.mountSocketRom(*found->image, found->id);
    }
    diskPath_ = settings.value("media/disk").toString();
    tapePath_ = settings.value("media/tape").toString();
}

Hb1Window::~Hb1Window() = default;

void Hb1Window::tick() {
    machine_.runCycles(machine_.profile().cyclesPerFrame);
    ++framesRun_;
    tube_->update();
    if (framesRun_ % 25 == 0) {
        status_->setText(tr("frame %1  ·  %2 cycles  ·  %3 opcodes")
                             .arg(machine_.frameCounter())
                             .arg(machine_.cpu6502().cycleCount())
                             .arg(machine_.cpu6502().instructionCount()));
    }
}

void Hb1Window::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            machine_.type(static_cast<std::uint8_t>(13));
            return;
        case Qt::Key_Backspace:
            machine_.type(static_cast<std::uint8_t>(8));
            return;
        case Qt::Key_Escape:
            machine_.type(static_cast<std::uint8_t>(27));
            return;
        case Qt::Key_Delete:
            machine_.type(static_cast<std::uint8_t>(127));
            return;
        case Qt::Key_Left:
            machine_.type(static_cast<std::uint8_t>(28));
            return;
        case Qt::Key_Right:
            machine_.type(static_cast<std::uint8_t>(29));
            return;
        case Qt::Key_Up:
            machine_.type(static_cast<std::uint8_t>(30));
            return;
        case Qt::Key_Down:
            machine_.type(static_cast<std::uint8_t>(31));
            return;
        case Qt::Key_Q:
            if ((event->modifiers() & Qt::ControlModifier) != 0) {
                close();
                return;
            }
            break;
        default:
            break;
    }
    const QString text = event->text();
    for (const QChar ch : text) {
        const ushort code = ch.unicode();
        if (code >= 32 && code < 127) {
            machine_.type(static_cast<std::uint8_t>(code));
        }
    }
}

}  // namespace timewarp::hb1
