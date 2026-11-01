// SPDX-License-Identifier: MIT

#include "timewarp/hb2/hbos.hpp"

#include "timewarp/hb2/font.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>

namespace timewarp::hb2 {

namespace {

// A 1992 workstation ran 640 x 480, and its text was twice the height of the
// 1982 tube's. `kScale` is that: every glyph is drawn two pixels wide and two
// tall, which is what makes this look like a GUI of the period rather than a
// small bitmap blown up.
constexpr int kScale = 2;
constexpr int kCellWidth = kGlyphWidth * kScale;
constexpr int kCellHeight = kGlyphHeight * kScale;
constexpr int kBorder = 1;
constexpr int kTitleHeight = kCellHeight + 6;
constexpr int kMenuHeight = kCellHeight + 4;
constexpr int kTaskBarHeight = kCellHeight + 10;

constexpr std::uint8_t kTitleInk = 15;
constexpr std::uint8_t kTitleIdleInk = 0;
constexpr std::uint8_t kTitleFocused = 1;   // the blue of the period
constexpr std::uint8_t kTitleIdle = 7;
constexpr std::uint8_t kBodyInk = 7;
constexpr std::uint8_t kBodyText = 0;
constexpr std::uint8_t kShadow = 0;
constexpr std::uint8_t kDesktop = 3;        // teal, the 1992 default
constexpr std::uint8_t kBarInk = 7;
constexpr std::uint8_t kBarText = 0;
constexpr std::uint8_t kBarButton = 1;

/// Text at the desktop's cell size: runs of set bits are filled a stretch at a
/// time rather than a pixel at a time, because this runs on every frame.
void drawTextScaled(Video& video, int x, int y, std::string_view text,
                    std::uint8_t colour) {
    int pen = x;
    for (const char character : text) {
        if (character == '\n') {
            pen = x;
            y += kCellHeight;
            continue;
        }
        const auto& glyph =
            glyphFor(static_cast<std::uint8_t>(static_cast<unsigned char>(character)));
        for (int row = 0; row < kGlyphHeight; ++row) {
            const std::uint8_t bits = glyph[static_cast<std::size_t>(row)];
            if (bits == 0) {
                continue;
            }
            int column = 0;
            while (column < kGlyphWidth) {
                if ((bits & (0x80U >> column)) == 0) {
                    ++column;
                    continue;
                }
                const int run_start = column;
                while (column < kGlyphWidth && (bits & (0x80U >> column)) != 0) {
                    ++column;
                }
                video.fill(pen + run_start * kScale, y + row * kScale,
                           (column - run_start) * kScale, kScale, colour);
            }
        }
        pen += kCellWidth;
    }
}

std::string padded(std::string text, std::size_t width) {
    if (text.size() > width) {
        text.resize(width);
    }
    while (text.size() < width) {
        text.push_back(' ');
    }
    return text;
}

}  // namespace

// ---------------------------------------------------------------------------
// The scheduler
// ---------------------------------------------------------------------------

Scheduler::Scheduler(Bus& bus) : bus_(bus) {}

void Scheduler::clear() {
    tasks_.clear();
    next_id_ = 1;
    next_ = 0;
    current_ = 0;
    next_address_ = kFirstAddress;
    error_.clear();
}

int Scheduler::spawn(const std::string& name, const std::string& source, std::uint32_t entry) {
    error_.clear();
    if (static_cast<int>(tasks_.size()) >= kMaxTasks) {
        error_ = "?NO ROOM: eight processes";
        return 0;
    }
    Assembly assembly = assemble(source, entry == 0 ? next_address_ : entry);
    if (!assembly.ok) {
        error_ = assembly.errors.empty() ? "?ASSEMBLY FAILED" : "? " + assembly.errors.front();
        return 0;
    }
    if (assembly.bytes.size() > kTaskBytes) {
        error_ = "?PROGRAM TOO LARGE";
        return 0;
    }
    const std::uint32_t load_at = assembly.origin;
    // A program that names its own ORG can land on another process's code, and two
    // processes running each other's instructions is a bug that looks like a scheduler
    // fault. The overlap is refused, and the sentence says which address is in use.
    for (const Task& existing : tasks_) {
        const std::uint32_t end = load_at + static_cast<std::uint32_t>(assembly.bytes.size());
        if (load_at < existing.load_at + existing.load_bytes && existing.load_at < end) {
            error_ = "?ADDRESS IN USE: $" + std::to_string(existing.load_at);
            return 0;
        }
    }
    for (std::size_t index = 0; index < assembly.bytes.size(); ++index) {
        bus_.write(load_at + static_cast<std::uint32_t>(index), assembly.bytes[index]);
    }

    // A cascade of small windows, so eight of them are all visible: the desktop's
    // own arrangement, not something a program has to compute.
    const int slot = static_cast<int>(tasks_.size());
    const Rect frame{8 + slot * 12, 8 + slot * 10, 150, 88};
    tasks_.emplace_back(next_id_, name, bus_, assembly.entry != 0 ? assembly.entry : load_at,
                        frame);
    tasks_.back().source = source;
    tasks_.back().load_at = load_at;
    tasks_.back().load_bytes = static_cast<std::uint32_t>(assembly.bytes.size());
    next_address_ = std::max(next_address_, load_at + kTaskBytes);
    return next_id_++;
}

int Scheduler::tick(std::uint32_t quantum) {
    int ran = 0;
    for (std::size_t step = 0; step < tasks_.size(); ++step) {
        const std::size_t index = (next_ + step) % tasks_.size();
        Task& task = tasks_[index];
        if (!task.ready || task.finished) {
            continue;
        }
        current_ = task.id;
        task.cpu.run(quantum);
        ++ran;
        switch (task.cpu.status()) {
        case CpuStatus::Running:
            break;
        case CpuStatus::Halted:
            task.finished = true;
            task.ready = false;
            break;
        case CpuStatus::Faulted:
            // A process that throws is killed and named, and the desktop survives it.
            task.finished = true;
            task.ready = false;
            task.failure = task.cpu.fault();
            break;
        }
    }
    current_ = 0;
    if (!tasks_.empty()) {
        next_ = (next_ + 1) % tasks_.size();
    }
    return ran;
}

Task* Scheduler::byId(int id) {
    for (Task& task : tasks_) {
        if (task.id == id) {
            return &task;
        }
    }
    return nullptr;
}

const Task* Scheduler::byId(int id) const {
    for (const Task& task : tasks_) {
        if (task.id == id) {
            return &task;
        }
    }
    return nullptr;
}

std::vector<std::string> Scheduler::status() const {
    std::vector<std::string> rows;
    for (const Task& task : tasks_) {
        std::string row = padded(task.name, 12);
        row += task.finished ? (task.failure.empty() ? "done " : "dead ") : "ready";
        row += "  ";
        row += std::to_string(task.window.w) + "x" + std::to_string(task.window.h);
        row += " at " + std::to_string(task.window.x) + "," + std::to_string(task.window.y);
        if (!task.failure.empty()) {
            row += "  " + task.failure;
        } else if (task.finished && task.exit_code != 0) {
            row += "  exit " + std::to_string(task.exit_code);
        }
        rows.push_back(row);
    }
    return rows;
}

// ---------------------------------------------------------------------------
// The desktop
// ---------------------------------------------------------------------------

Desktop::Desktop() = default;

void Desktop::clear() {
    windows_.clear();
    next_z_ = 1;
    focused_ = 0;
}

void Desktop::addWindow(int task, const std::string& title, Rect frame) {
    removeWindow(task);
    windows_.push_back(Window{task, title, frame, next_z_++});
    focused_ = task;
}

void Desktop::removeWindow(int task) {
    windows_.erase(std::remove_if(windows_.begin(), windows_.end(),
                                  [task](const Window& window) { return window.task == task; }),
                   windows_.end());
    if (focused_ == task) {
        focused_ = windows_.empty() ? 0 : windows_.back().task;
    }
}

Desktop::Window* Desktop::findByTask(int task) {
    for (Window& window : windows_) {
        if (window.task == task) {
            return &window;
        }
    }
    return nullptr;
}

const Desktop::Window* Desktop::findByTask(int task) const {
    for (const Window& window : windows_) {
        if (window.task == task) {
            return &window;
        }
    }
    return nullptr;
}

void Desktop::focus(int task) {
    Window* window = findByTask(task);
    if (window == nullptr) {
        return;
    }
    window->z = next_z_++;
    focused_ = task;
}

void Desktop::move(int task, int x, int y) {
    if (Window* window = findByTask(task)) {
        window->frame.x = x;
        window->frame.y = y;
    }
}

void Desktop::resize(int task, int w, int h) {
    if (Window* window = findByTask(task)) {
        window->frame.w = std::max(40, w);
        window->frame.h = std::max(30, h);
    }
}

Rect Desktop::frameOf(int task) const {
    const Window* window = findByTask(task);
    return window == nullptr ? Rect{} : window->frame;
}

int Desktop::topAt(int x, int y) const {
    // Topmost first: the highest z that contains the point is the one the pointer is
    // over, which is the whole of hit testing.
    const Window* top = nullptr;
    for (const Window& window : windows_) {
        if (window.frame.contains(x, y) && (top == nullptr || window.z > top->z)) {
            top = &window;
        }
    }
    return top == nullptr ? 0 : top->task;
}

void Desktop::movePointer(int x, int y) {
    pointer_x_ = x;
    pointer_y_ = y;
}

void Desktop::drawWindow(Video& video, const Window& window, const Task& task, bool focused) const {
    const Rect r = window.frame;
    // Shadow, body, title bar: three rectangles, and the overlap only looks right
    // because the framebuffer composes rather than the character grid.
    video.fill(r.x + 3, r.y + 3, r.w, r.h, kShadow);
    video.fill(r.x, r.y, r.w, r.h, kBodyInk);
    // A one-pixel frame round the client area, the raised edge every window of the
    // period had.
    video.fill(r.x, r.y, 1, r.h, 15);
    video.fill(r.x, r.y, r.w, 1, 15);
    video.fill(r.x + r.w - 1, r.y, 1, r.h, 8);
    video.fill(r.x, r.y + r.h - 1, r.w, 1, 8);
    video.fill(r.x, r.y, r.w, kTitleHeight, focused ? kTitleFocused : kTitleIdle);
    drawTextScaled(video, r.x + 5, r.y + 3,
                   padded(window.title, static_cast<std::size_t>((r.w - 10) / kCellWidth)),
                   focused ? kTitleInk : kTitleIdleInk);
    // A menu bar, because a 1992 application window without one is a terminal
    // with a title. The words do what the prompt's words do; this is their face.
    video.fill(r.x + 1, r.y + kTitleHeight, r.w - 2, 1, 8);
    drawTextScaled(video, r.x + 6, r.y + kTitleHeight + 2, "File Edit View Help", kBodyText);
    // The task's own output, last lines that fit, which is the whole of what a
    // process's window is for on this machine.
    const int columns = std::max(1, (r.w - 10) / kCellWidth);
    const int rows = std::max(1, (r.h - kTitleHeight - kMenuHeight - 4) / kCellHeight);
    std::vector<std::string> lines;
    std::string current;
    for (const char character : task.console) {
        if (character == '\n') {
            lines.push_back(current);
            current.clear();
            continue;
        }
        if (current.size() == static_cast<std::size_t>(columns)) {
            lines.push_back(current);
            current.clear();
        }
        current.push_back(character);
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    const int first = std::max(0, static_cast<int>(lines.size()) - rows);
    for (int row = first; row < static_cast<int>(lines.size()); ++row) {
        drawTextScaled(video, r.x + 5,
                       r.y + kTitleHeight + kMenuHeight + 2 + (row - first) * kCellHeight,
                       lines[static_cast<std::size_t>(row)], kBodyText);
    }
    if (!task.finished) {
        // A cursor block, because a process with a window should look alive. It
        // sits where the next character will land: after the partial line when it
        // is on screen, and at the end of the last visible line when the window is
        // full, rather than over the first character of it.
        int caret_row = static_cast<int>(lines.size()) - first;
        int caret_column = static_cast<int>(current.size());
        if (caret_row >= rows) {
            caret_row = rows - 1;
            const std::string& last = lines[static_cast<std::size_t>(first + caret_row)];
            caret_column = static_cast<int>(last.size());
        }
        video.fill(r.x + 5 + std::min(columns - 1, caret_column) * kCellWidth,
                   r.y + kTitleHeight + kMenuHeight + 2 + caret_row * kCellHeight,
                   kCellWidth - 3, kCellHeight - 3, kBodyText);
    }
}

void Desktop::draw(Video& video, const Scheduler& scheduler) {
    if (video.mode() == Video::Mode::Text) {
        return;
    }
    const int width = video.width();
    const int height = video.height();
    // The wallpaper, then the bar, then the windows bottom to top: a 1992 desktop
    // is a painter's algorithm and nothing here pretends otherwise.
    video.fill(0, 0, width, height, kDesktop);
    video.fill(0, height - kTaskBarHeight, width, kTaskBarHeight, kBarInk);
    drawTextScaled(video, 8, height - kTaskBarHeight + 4, "HB-OS", kBarText);

    std::vector<const Window*> ordered;
    for (const Window& window : windows_) {
        ordered.push_back(&window);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const Window* left, const Window* right) { return left->z < right->z; });

    int button = 6 * kCellWidth;
    for (const Window* window : ordered) {
        const Task* task = scheduler.byId(window->task);
        if (task != nullptr) {
            drawWindow(video, *window, *task, window->task == focused_);
        }
        // One button a process, which is what an icon bar is.
        const int button_width = 10 * kCellWidth;
        video.fill(button, height - kTaskBarHeight + 4, button_width, kTaskBarHeight - 8,
                   window->task == focused_ ? kBarButton : kBarInk);
        drawTextScaled(video, button + 4, height - kTaskBarHeight + 4,
                       padded(window->title, 9),
                       window->task == focused_ ? kTitleInk : kBarText);
        button += button_width + 4;
    }

    // The pointer: four pixels wide and eight tall, drawn as two lines so it is
    // visible over anything -- the machine's own arrow, not a cursor image.
    video.line(pointer_x_, pointer_y_, pointer_x_, pointer_y_ + 7, 15);
    video.line(pointer_x_, pointer_y_ + 7, pointer_x_ + 4, pointer_y_ + 4, 15);
    video.line(pointer_x_, pointer_y_, pointer_x_ + 4, pointer_y_ + 4, 15);
}

}  // namespace timewarp::hb2
