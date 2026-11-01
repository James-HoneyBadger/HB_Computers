// SPDX-License-Identifier: MIT
//
// The devices both machines are made of.
//
// The Kestrel and the Kestrel 16 are the same design ten years apart, and their tube, their
// keyboard, their disk and their cassette are the same devices with different
// numbers attached: a matrix of character cells with an attribute byte each, a
// queue of keystrokes, a store of sectors, and a tape with a play head. The
// numbers come from the machine's profile; the behaviour lives here, once.
//
// Nothing here knows about a processor, an address or a language. The machine
// decides which byte of RAM is a cell or a register, and calls these.

#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace kestrel {

/// The keys the machines name. Printable characters arrive as their own ASCII
/// code, so a value here is only for the ones that produce no character.
enum class SpecialKey : std::uint8_t {
    Return = 0x0D,
    Backspace = 0x08,
    Delete = 0x08,
    Escape = 0x1B,
    Break = 0x03,
    Left = 0x1C,
    Right = 0x1D,
    Up = 0x1E,
    Down = 0x1F,
};

/// A matrix of character cells, each with one attribute byte.
///
/// The cells and the attributes are separate arrays because both machines put
/// them at separate addresses; a host that painted its own screen would be the
/// thing these machines exist not to be.
class Video {
public:
    Video(int columns, int rows);

    /// Spaces in the given colours: the screen a machine comes up with.
    void powerOn(std::uint8_t ink, std::uint8_t paper);

    [[nodiscard]] int columns() const noexcept { return columns_; }
    [[nodiscard]] int rows() const noexcept { return rows_; }
    [[nodiscard]] int cells() const noexcept { return cellCount_; }

    [[nodiscard]] std::uint8_t cell(int index) const;
    void setCell(int index, std::uint8_t code);
    [[nodiscard]] std::uint8_t attribute(int index) const;
    void setAttribute(int index, std::uint8_t value);
    [[nodiscard]] std::uint8_t cellAt(int row, int column) const;

    /// One row as text, trailing spaces removed. The cursor is not part of the
    /// text: it is the host's to draw.
    [[nodiscard]] std::string line(int row) const;
    /// Every row, newline-separated.
    [[nodiscard]] std::string text() const;

private:
    int columns_;
    int rows_;
    int cellCount_;
    std::vector<std::uint8_t> cells_;
    std::vector<std::uint8_t> attributes_;
};

/// The keyboard controller: a queue of bytes the ROM reads one at a time.
class Keyboard {
public:
    explicit Keyboard(int capacity);

    /// Queues a keystroke. False when the queue is full, which is what a person
    /// typing faster than the machine would discover.
    bool push(std::uint8_t key);

    /// The oldest key, or zero when there is none.
    std::uint8_t read();
    [[nodiscard]] bool available() const noexcept { return !buffer_.empty(); }
    [[nodiscard]] int size() const noexcept {
        return static_cast<int>(buffer_.size());
    }
    void clear() noexcept { buffer_.clear(); }
    [[nodiscard]] std::uint8_t lastKey() const noexcept { return lastKey_; }

private:
    int capacity_;
    std::deque<std::uint8_t> buffer_;
    std::uint8_t lastKey_ = 0;
};

/// The disk: a store of sectors, and nothing more.
///
/// The filesystem is not here. Each machine's DOS is machine code in its ROM and
/// works through these sectors, which is why a program that saves a file
/// exercises the ROM rather than the emulator.
class Disk {
public:
    Disk(int sectors, int sectorSize);

    [[nodiscard]] bool present() const noexcept { return present_; }
    void insert();
    void eject() noexcept;

    /// A blank volume: a signature and a volume name, and nothing else.
    void format(std::string_view volume = "KESWARP");
    [[nodiscard]] std::string volumeName() const;

    [[nodiscard]] int sectors() const noexcept { return sectors_; }
    [[nodiscard]] int sectorSize() const noexcept { return sectorSize_; }
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
        return bytes_;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& image() const noexcept {
        return bytes_;
    }

    /// Mounts a volume from the host. A volume of the wrong size is refused:
    /// half a disk is a worse lie than no disk.
    bool load(const std::vector<std::uint8_t>& bytes);
    void loadOrFormat(const std::vector<std::uint8_t>& bytes,
                      std::string_view volume = "KESWARP");

    [[nodiscard]] std::uint8_t* sector(int index);
    [[nodiscard]] const std::uint8_t* sector(int index) const;
    [[nodiscard]] bool readSector(std::uint32_t sector, std::uint8_t* into) const;
    bool writeSector(std::uint32_t sector, const std::uint8_t* from);

private:
    int sectors_;
    int sectorSize_;
    std::vector<std::uint8_t> bytes_;
    bool present_ = true;
};

/// The cassette: a byte stream with a play head, which is all a tape ever was to
/// the machine. The block format belongs to the KERNAL.
class Tape {
public:
    enum class Mode { Stopped, Playing, Recording };

    void insert() noexcept { present_ = true; }
    void eject() noexcept {
        present_ = false;
        mode_ = Mode::Stopped;
    }
    [[nodiscard]] bool present() const noexcept { return present_; }

    void load(const std::vector<std::uint8_t>& bytes);
    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
        return bytes_;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& contents() const noexcept {
        return bytes_;
    }

    void play() noexcept { mode_ = Mode::Playing; }
    void record() noexcept { mode_ = Mode::Recording; }
    void stop() noexcept { mode_ = Mode::Stopped; }
    void rewind() noexcept { position_ = 0; }
    [[nodiscard]] Mode mode() const noexcept { return mode_; }

    void writeByte(std::uint8_t value);
    [[nodiscard]] bool canRead() const noexcept {
        return mode_ == Mode::Playing && position_ < bytes_.size();
    }
    [[nodiscard]] bool dataLeft() const noexcept { return canRead(); }
    std::uint8_t readByte();

    /// bit 7 present, bit 6 playing, bit 5 recording, bit 4 more to read.
    [[nodiscard]] std::uint8_t status() const noexcept;

    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    void setPosition(std::size_t position) noexcept;

private:
    std::vector<std::uint8_t> bytes_;
    std::size_t position_ = 0;
    Mode mode_ = Mode::Stopped;
    bool present_ = false;
};

}  // namespace kestrel
