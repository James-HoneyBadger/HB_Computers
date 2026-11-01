// SPDX-License-Identifier: MIT
//
// The glass over the machines.
//
// `CrtOverlay` draws scanlines, vignette, curvature, flicker and a phosphor tint
// on top of whichever widget it covers, and `CrtSettingsDialog` is the five
// sliders that tune it. Both machines use it: the HB-1 as its whole face, and the
// HB-2 as the same monitor seen in 1992.
//
// The overlay installs itself as an event filter on the widget it covers rather
// than reaching into `parent().rect()`, so reparenting it to the viewport of a
// scroll area cannot silently stop it from tracking.

#pragma once

#include <QColor>
#include <QDialog>
#include <QWidget>

class QEvent;
class QPainter;
class QPaintEvent;
class QTimer;

namespace timewarp::ui {

/// Stands in for `CRTEffectOverlay` (`ui/crt_effect.py`).
///
/// The Python overlay draws scanlines, curvature, vignette, glow and flicker and
/// exposes sliders for each. Only scanlines are implemented: the point of
/// including it at all is the *geometry tracking*, which is where the Python
/// approach is fragile.
///
/// The Python resizes the overlay from `MainWindow.resizeEvent` by reaching into
/// `overlay.parent().rect()`. That only holds while the overlay's parent happens
/// to be the widget being resized; reparenting it -- to the viewport of a
/// scroll area, say -- silently stops it from tracking. This version installs
/// itself as an event filter on the widget it covers, so the coupling is stated
/// instead of assumed.
class CrtOverlay final : public QWidget {
    Q_OBJECT

public:
    explicit CrtOverlay(QWidget* target);

    /// The overlay's five effects, each 0..100. Zero everywhere paints nothing at
    /// all, which is what makes "off" exact rather than nearly-nothing.
    struct Settings {
        int scanlines = 15;
        int vignette = 0;
        /// A rounded, darkened border suggesting a tube's curved face. Not a real
        /// warp: distorting the pixels underneath would mean rendering the window
        /// through a shader, and the honest version of the effect is a bezel.
        int curvature = 0;
        /// A slow brightness breath. Subtle by design -- a flicker a learner notices
        /// while reading is a flicker that hurts.
        int flicker = 0;
        /// A tint over everything, the amber or green cast of a phosphor monitor.
        int phosphor = 0;
        QColor phosphor_colour{51, 255, 102};
    };

    void setSettings(const Settings& settings);
    [[nodiscard]] const Settings& settings() const noexcept { return settings_; }

    /// 0..100. Zero disables scanlines without hiding the widget. Kept as its own
    /// accessor because the View menu toggles the overlay's visibility, not this.
    void setScanlineIntensity(int percent);
    [[nodiscard]] int scanlineIntensity() const noexcept;

    /// The widget this overlay tracks, or null if it has been destroyed.
    [[nodiscard]] QWidget* target() const noexcept;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void syncToTarget();
    void paintScanlines(QPainter& painter);
    void paintVignette(QPainter& painter);
    void paintCurvature(QPainter& painter);
    void paintPhosphor(QPainter& painter);

    QWidget* target_ = nullptr;
    Settings settings_;
    /// Flicker needs a clock; the other four are still.
    QTimer* flicker_timer_ = nullptr;
    int flicker_phase_ = 0;
};

/// The CRT settings dialog: five sliders and a live preview.
///
/// A dialog rather than panel controls because the effect is set once and then
/// forgotten, and because the preview has to be the real overlay: a settings
/// window that shows its own mock-up can be right while the window is wrong.
class CrtSettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit CrtSettingsDialog(const CrtOverlay::Settings& start, QWidget* parent = nullptr);

    [[nodiscard]] CrtOverlay::Settings settings() const noexcept { return settings_; }

signals:
    /// Emitted on every slider move, so the window can paint the change before the
    /// user decides. `rejected` is the window's cue to put the old values back.
    void previewRequested(const CrtOverlay::Settings& settings);

private:
    CrtOverlay::Settings settings_;
};

}  // namespace timewarp::ui
