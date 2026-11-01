// SPDX-License-Identifier: MIT
//
// The HB-2's bus. Every address the processor puts on the wire is decoded here:
// RAM, the two ROM banks, the cartridge, the tube's two arrays and the I/O page.

#include "timewarp/hb2/machine.hpp"

#include <algorithm>
#include <cstring>

namespace timewarp::hb2 {

namespace {

constexpr std::uint32_t kAttributeEnd =
    map::kAttributeBase + map::kScreenCells;
constexpr std::uint32_t kRamSize = 0x100000;  // banks $00..$0F

}  // namespace

Hb2Machine::Hb2Machine() : ram_(kRamSize, 0) {
    kernal_.assign(map::kKernalRomSize, 0xFF);
    basicRom_.assign(static_cast<std::size_t>(map::kBasicRomSize), 0xFF);
}

int Hb2Machine::cyclesPerSecond() const noexcept {
    switch (speed_) {
    case Speed::Mhz1: return 1'000'000;
    case Speed::Mhz2: return 2'000'000;
    case Speed::Mhz8: return kClockHz;
    case Speed::Turbo: return 0;  // as fast as the host can
    }
    return kClockHz;
}

bool Hb2Machine::mountKernal(const std::vector<std::uint8_t>& image,
                             std::string_view id) {
    if (image.size() != map::kKernalRomSize) {
        return false;
    }
    kernal_ = image;
    kernalId_ = id;
    return true;
}

bool Hb2Machine::mountBasic(const std::vector<std::uint8_t>& image,
                            std::string_view id) {
    if (image.size() != map::kBasicRomSize) {
        return false;
    }
    basicRom_ = image;
    basicId_ = id;
    return true;
}

bool Hb2Machine::mountCartridge(const std::vector<std::uint8_t>& image,
                                std::string_view id) {
    constexpr std::size_t kCartridgeSize =
        static_cast<std::size_t>(map::kCartridgeBankCount) * 0x10000;
    if (image.size() != kCartridgeSize) {
        return false;
    }
    cartridge_ = image;
    cartridgeId_ = id;
    cartridgeMounted_ = true;
    return true;
}

void Hb2Machine::ejectCartridge() noexcept {
    cartridge_.clear();
    cartridgeId_.clear();
    cartridgeMounted_ = false;
}

void Hb2Machine::reset() {
    std::fill(ram_.begin(), ram_.end(), 0);
    video_.powerOn();
    keyboard_.clear();
    cursorColumn_ = 0;
    cursorRow_ = 0;
    cursorOn_ = true;
    scrolled_ = false;
    ink_ = map::kLightGrey;
    paper_ = map::kBlack;
    border_ = map::kLightBlue;
    frameCounter_ = 0;
    frameFlag_ = false;
    cyclesToFrame_ = 0;
    soundLatch_ = 0;
    notesPlayed_ = 0;
    randomState_ = 0xACE1;
    diskSector_ = 0;
    diskAddress_ = 0;
    diskCount_ = 0;
    diskStatus_ = disk_.present() ? map::kDiskPresent : 0;
    cpu_.reset();
}

std::uint8_t Hb2Machine::ramAt(std::uint32_t address) const {
    return address < kRamSize ? ram_[address] : 0;
}

void Hb2Machine::setRamAt(std::uint32_t address, std::uint8_t value) {
    if (address < kRamSize) {
        ram_[address] = value;
    }
}

std::uint8_t Hb2Machine::read(std::uint32_t address) {
    const std::uint8_t bank = static_cast<std::uint8_t>(address >> 16);
    const std::uint16_t offset = static_cast<std::uint16_t>(address & 0xFFFF);

    if (bank == 0) {
        if (offset >= map::kKernalRomBase) {
            return kernal_[offset - map::kKernalRomBase];
        }
        if (offset >= map::kCartridgeBase &&
            offset < map::kCartridgeBase + map::kCartridgeSize) {
            if (!cartridgeMounted_) {
                return 0;
            }
            return cartridge_[offset - map::kCartridgeBase];
        }
        if (offset >= map::kScreenBase && offset < map::kAttributeBase) {
            return video_.cell(offset - map::kScreenBase);
        }
        if (offset >= map::kAttributeBase && offset < kAttributeEnd) {
            return video_.attribute(offset - map::kAttributeBase);
        }
        if (offset >= map::kIoBase && offset < map::kIoBase + 0x100) {
            return readRegister(static_cast<std::uint16_t>(offset - map::kIoBase));
        }
        if (offset >= map::kIoBase) {
            return 0;
        }
        return ram_[address];
    }
    if (bank == map::kBasicBank) {
        return basicRom_[offset];
    }
    if (bank >= map::kCartridgeFirstBank &&
        bank < map::kCartridgeFirstBank + map::kCartridgeBankCount) {
        if (!cartridgeMounted_) {
            return 0;
        }
        const std::size_t index =
            static_cast<std::size_t>(bank - map::kCartridgeFirstBank) * 0x10000 +
            offset;
        return cartridge_[index];
    }
    if (address < kRamSize) {
        return ram_[address];
    }
    return 0;
}

void Hb2Machine::write(std::uint32_t address, std::uint8_t value) {
    const std::uint8_t bank = static_cast<std::uint8_t>(address >> 16);
    const std::uint16_t offset = static_cast<std::uint16_t>(address & 0xFFFF);

    if (bank == 0) {
        if (offset >= map::kKernalRomBase) {
            return;  // ROM: writes are lost
        }
        if (offset >= map::kCartridgeBase &&
            offset < map::kCartridgeBase + map::kCartridgeSize) {
            return;
        }
        if (offset >= map::kScreenBase && offset < map::kAttributeBase) {
            video_.setCell(offset - map::kScreenBase, value);
            return;
        }
        if (offset >= map::kAttributeBase && offset < kAttributeEnd) {
            video_.setAttribute(offset - map::kAttributeBase, value);
            return;
        }
        if (offset >= map::kIoBase && offset < map::kIoBase + 0x100) {
            writeRegister(static_cast<std::uint16_t>(offset - map::kIoBase),
                          value);
            return;
        }
        if (offset >= map::kIoBase) {
            return;
        }
        ram_[address] = value;
        return;
    }
    if (bank == map::kBasicBank) {
        return;
    }
    if (bank >= map::kCartridgeFirstBank &&
        bank < map::kCartridgeFirstBank + map::kCartridgeBankCount) {
        return;
    }
    if (address < kRamSize) {
        ram_[address] = value;
    }
}

std::uint8_t Hb2Machine::readRegister(std::uint16_t offset) {
    switch (offset) {
    case map::kKeyboardData: {
        const std::uint8_t key = keyboard_.read();
        if (key != 0) {
            lastKey_ = key;
        }
        return key;
    }
    case map::kKeyboardStatus:
        return keyboard_.available() ? 0x80 : 0x00;
    case map::kCursorColumn: return cursorColumn_;
    case map::kCursorRow: return cursorRow_;
    case map::kInk: return ink_;
    case map::kPaper: return paper_;
    case map::kBorder: return border_;
    case map::kFrameLow:
        return static_cast<std::uint8_t>(frameCounter_ & 0xFF);
    case map::kFrameHigh:
        return static_cast<std::uint8_t>(frameCounter_ >> 8);
    case map::kFrameFlag: return frameFlag_ ? 0x80 : 0x00;
    case map::kDiskStatus: return diskStatus_;
    case map::kRandom: return randomByte();
    case map::kLastKey: return lastKey_;
    case map::kScrolled: return scrolled_ ? 0x80 : 0x00;
    case map::kCursorOn: return cursorOn_ ? 0x80 : 0x00;
    case map::kTapeData: return tape_.readByte();
    case map::kTapeStatus: {
        std::uint8_t status = 0;
        if (tape_.present()) {
            status |= map::kTapePresent;
        }
        if (tape_.mode() == Hb2Tape::Mode::Playing) {
            status |= map::kTapePlaying;
        }
        if (tape_.mode() == Hb2Tape::Mode::Recording) {
            status |= map::kTapeRecording;
        }
        if (tape_.dataLeft()) {
            status |= map::kTapeDataLeft;
        }
        return status;
    }
    default:
        return 0;
    }
}

void Hb2Machine::writeRegister(std::uint16_t offset, std::uint8_t value) {
    switch (offset) {
    case map::kKeyboardClear:
        keyboard_.clear();
        break;
    case map::kCursorColumn:
        cursorColumn_ = value;
        if (cursorColumn_ >= map::kScreenColumns) {
            cursorColumn_ = map::kScreenColumns - 1;
        }
        break;
    case map::kCursorRow:
        cursorRow_ = value;
        if (cursorRow_ >= map::kScreenRows) {
            cursorRow_ = map::kScreenRows - 1;
        }
        break;
    case map::kInk: ink_ = static_cast<std::uint8_t>(value & 0x0F); break;
    case map::kPaper: paper_ = static_cast<std::uint8_t>(value & 0x0F); break;
    case map::kBorder: border_ = static_cast<std::uint8_t>(value & 0x0F); break;
    case map::kSound:
        soundLatch_ = value;
        ++notesPlayed_;
        if (onSound) {
            onSound(value);
        }
        break;
    case map::kFrameFlag:
        frameFlag_ = false;
        break;
    case map::kDiskSectorLow:
        diskSector_ = (diskSector_ & 0xFFFFFF00u) | value;
        break;
    case map::kDiskSectorHigh:
        diskSector_ = (diskSector_ & 0xFFFF00FFu) |
                      (static_cast<std::uint32_t>(value) << 8);
        break;
    case map::kDiskCommand:
        executeDiskCommand(value);
        break;
    case map::kDiskAddressLow:
        diskAddress_ = (diskAddress_ & 0xFFFF00u) | value;
        break;
    case map::kDiskAddressHigh:
        diskAddress_ = (diskAddress_ & 0xFF00FFu) |
                       (static_cast<std::uint32_t>(value) << 8);
        break;
    case map::kDiskAddressBank:
        diskAddress_ = (diskAddress_ & 0x00FFFFu) |
                       (static_cast<std::uint32_t>(value) << 16);
        break;
    case map::kDiskCountLow:
        diskCount_ = static_cast<std::uint16_t>((diskCount_ & 0xFF00) | value);
        break;
    case map::kDiskCountHigh:
        diskCount_ = static_cast<std::uint16_t>((diskCount_ & 0x00FF) |
                                                (value << 8));
        break;
    case map::kScrolled:
        scrolled_ = (value & 0x80) != 0;
        break;
    case map::kCursorOn:
        cursorOn_ = (value & 0x80) != 0;
        break;
    case map::kTapeData:
        tape_.writeByte(value);
        break;
    case map::kTapeControl:
        switch (value) {
        case map::kTapePlay: tape_.play(); break;
        case map::kTapeRecord: tape_.record(); break;
        case map::kTapeStop: tape_.stop(); break;
        case map::kTapeRewind: tape_.rewind(); break;
        default: break;
        }
        break;
    default:
        break;
    }
}

void Hb2Machine::executeDiskCommand(std::uint8_t command) {
    diskStatus_ = disk_.present() ? map::kDiskPresent : 0;
    if (!disk_.present()) {
        diskStatus_ |= map::kDiskError;
        return;
    }
    const std::uint16_t count = diskCount_ == 0 ? 1 : diskCount_;
    switch (command) {
    case map::kDiskFormat:
        disk_.format();
        break;
    case map::kDiskQuery:
        break;
    case map::kDiskRead:
    case map::kDiskWrite: {
        std::array<std::uint8_t, map::kDiskSectorSize> buffer{};
        std::uint32_t address = diskAddress_;
        for (std::uint16_t i = 0; i < count; ++i) {
            const std::uint32_t sector = diskSector_ + i;
            if (command == map::kDiskRead) {
                if (!disk_.readSector(sector, buffer.data())) {
                    diskStatus_ |= map::kDiskError;
                    return;
                }
                for (std::size_t b = 0; b < buffer.size(); ++b) {
                    write(address + static_cast<std::uint32_t>(b), buffer[b]);
                }
            } else {
                for (std::size_t b = 0; b < buffer.size(); ++b) {
                    buffer[b] = read(address + static_cast<std::uint32_t>(b));
                }
                if (!disk_.writeSector(sector, buffer.data())) {
                    diskStatus_ |= map::kDiskError;
                    return;
                }
            }
            address += map::kDiskSectorSize;
        }
        break;
    }
    default:
        diskStatus_ |= map::kDiskError;
        return;
    }
    diskStatus_ |= map::kDiskDone;
}

std::uint8_t Hb2Machine::randomByte() {
    // A maximal-length shift register, the same shape the HB-1 uses: cheap, and
    // repeatable, which is the only property a test needs.
    const std::uint16_t bit =
        static_cast<std::uint16_t>((randomState_ ^ (randomState_ >> 1)) & 1);
    randomState_ = static_cast<std::uint16_t>((randomState_ >> 1) | (bit << 15));
    return static_cast<std::uint8_t>(randomState_ & 0xFF);
}

void Hb2Machine::tickFrame() {
    ++frameCounter_;
    frameFlag_ = true;
}

void Hb2Machine::runCycles(long cycles) {
    long done = 0;
    while (done < cycles) {
        cpu_.setIrqLine(interruptPending());
        const int taken = cpu_.step();
        done += taken;
        cyclesToFrame_ += static_cast<std::uint32_t>(taken);
        if (cyclesToFrame_ >= static_cast<std::uint32_t>(kCyclesPerFrame)) {
            cyclesToFrame_ -= static_cast<std::uint32_t>(kCyclesPerFrame);
            tickFrame();
        }
    }
}

void Hb2Machine::runFrames(int frames) {
    runCycles(static_cast<long>(frames) * kCyclesPerFrame);
}

void Hb2Machine::type(std::uint8_t key) {
    keyboard_.push(key);
}

void Hb2Machine::typeText(std::string_view text) {
    for (const char ch : text) {
        keyboard_.push(ch == '\n' ? static_cast<std::uint8_t>(SpecialKey::Return)
                                 : static_cast<std::uint8_t>(ch));
    }
}

void Hb2Machine::typeLine(std::string_view text) {
    typeText(text);
    keyboard_.push(static_cast<std::uint8_t>(SpecialKey::Return));
}

void Hb2Machine::pressBreak() {
    cpu_.requestNmi();
}

}  // namespace timewarp::hb2
