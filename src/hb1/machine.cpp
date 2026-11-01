// SPDX-License-Identifier: MIT

#include "timewarp/hb1/machine.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include "hb1/basic_rom.hpp"
#include "hb1/kernal_rom.hpp"

namespace timewarp::hb1 {

namespace {

constexpr std::uint16_t kScreenEnd =
    map::kScreenBase + map::kScreenCells;
constexpr std::uint16_t kAttributeEnd =
    map::kAttributeBase + map::kAttributeCells;
constexpr std::uint16_t kIoEnd = map::kIoBase + 0x100;
constexpr std::uint16_t kBasicRomEnd = map::kBasicRomBase + map::kBasicRomSize;

}  // namespace

Hb1Machine::Hb1Machine() {
    socketRom_.assign(rom::kBASICImage.begin(), rom::kBASICImage.end());
    reset();
}

int Hb1Machine::cyclesPerSecond() const noexcept {
    switch (speed_) {
        case Speed::Mhz1: return 1'000'000;
        case Speed::Mhz2: return 2'000'000;
        case Speed::Mhz4: return 4'000'000;
        case Speed::Turbo: return 0;
    }
    return 1'000'000;
}

bool Hb1Machine::mountSocketRom(const std::vector<std::uint8_t>& image,
                                std::string_view id) {
    if (image.size() != static_cast<std::size_t>(map::kBasicRomSize)) {
        return false;
    }
    socketRom_ = image;
    socketRomId_ = std::string{id};
    reset();
    return true;
}

bool Hb1Machine::mountCartridge(const std::vector<std::uint8_t>& image,
                                std::string_view id) {
    if (image.size() != static_cast<std::size_t>(map::kCartridgeSize)) {
        return false;
    }
    cartridge_ = image;
    cartridgeId_ = std::string{id};
    cartridgeMounted_ = true;
    reset();
    return true;
}

void Hb1Machine::ejectCartridge() noexcept {
    cartridgeMounted_ = false;
    cartridgeId_.clear();
    cartridge_.clear();
    reset();
}

void Hb1Machine::reset() {
    ram_.fill(0);
    video_.powerOn();
    keyboard_.clear();
    cursorColumn_ = 0;
    cursorRow_ = 0;
    ink_ = map::kLightGrey;
    paper_ = map::kBlack;
    border_ = map::kLightBlue;
    frameCounter_ = 0;
    frameFlag_ = false;
    cyclesToFrame_ = 0;
    cursorOn_ = false;
    scrolled_ = false;
    soundLatch_ = 0;
    notesPlayed_ = 0;
    randomState_ = 0xACE1;
    diskSector_ = 0;
    diskBuffer_ = 0;
    diskStatus_ = 0;
    diskSectorCount_ = disk_.present() ? Hb1Disk::kSectors : 0;
    if (socketRom_.empty()) {
        socketRom_.assign(rom::kBASICImage.begin(), rom::kBASICImage.end());
    }
    cpu_.setIrqLine(false);
    cpu_.reset();
}

std::uint8_t Hb1Machine::read(std::uint16_t address) {
    if (address <= map::kRamTop) {
        return ram_[address];
    }
    if (address >= map::kScreenBase && address < kScreenEnd) {
        return video_.cell(address - map::kScreenBase);
    }
    if (address >= map::kScreenBase && address < map::kAttributeBase) {
        // The padding between the matrix and the colour map is screen memory
        // too; it reads as spaces rather than as a hole.
        return static_cast<std::uint8_t>(' ');
    }
    if (address >= map::kAttributeBase && address < kAttributeEnd) {
        return video_.attribute(address - map::kAttributeBase);
    }
    if (address >= map::kIoBase && address < kIoEnd) {
        switch (address - map::kIoBase) {
            case map::kKeyboardData: return keyboard_.read();
            case map::kKeyboardStatus:
                return keyboard_.available() ? 0xFF : 0x00;
            case map::kCursorColumn:
                return static_cast<std::uint8_t>(cursorColumn_);
            case map::kCursorRow:
                return static_cast<std::uint8_t>(cursorRow_);
            case map::kInk: return ink_;
            case map::kPaper: return paper_;
            case map::kBorder: return border_;
            case map::kSound: return 0;
            case map::kFrameLow:
                return static_cast<std::uint8_t>(frameCounter_ & 0xFF);
            case map::kFrameHigh:
                return static_cast<std::uint8_t>(frameCounter_ >> 8);
            case map::kFrameFlag: return frameFlag_ ? 0xFF : 0x00;
            case map::kDiskStatus: return diskStatus_;
            case map::kDiskCountLow:
                return static_cast<std::uint8_t>(diskSectorCount_ & 0xFF);
            case map::kDiskCountHigh:
                return static_cast<std::uint8_t>(diskSectorCount_ >> 8);
            case map::kTapeData: return tape_.readByte();
            case map::kTapeStatus: return tape_.status();
            case map::kRandom: return randomByte();
            case map::kLastKey: return keyboard_.lastKey();
            case map::kScrolled: return scrolled_ ? 0xFF : 0x00;
            case map::kCursorOn: return cursorOn_ ? 0x01 : 0x00;
            default: return 0;
        }
    }
    if (address >= map::kCartridgeBase &&
        address < map::kCartridgeBase + map::kCartridgeSize) {
        if (!cartridgeMounted_) {
            return 0;
        }
        return cartridge_[address - map::kCartridgeBase];
    }
    if (address >= map::kBasicRomBase && address < kBasicRomEnd) {
        return socketRom_[address - map::kBasicRomBase];
    }
    if (address >= map::kKernalRomBase) {
        return rom::kKERNALImage[address - map::kKernalRomBase];
    }
    return 0;
}

void Hb1Machine::write(std::uint16_t address, std::uint8_t value) {
    if (address <= map::kRamTop) {
        ram_[address] = value;
        return;
    }
    if (address >= map::kScreenBase && address < kScreenEnd) {
        video_.setCell(address - map::kScreenBase, value);
        return;
    }
    if (address >= map::kAttributeBase && address < kAttributeEnd) {
        video_.setAttribute(address - map::kAttributeBase, value);
        return;
    }
    if (address >= map::kIoBase && address < kIoEnd) {
        switch (address - map::kIoBase) {
            case map::kKeyboardClear: keyboard_.clear(); break;
            case map::kCursorColumn: cursorColumn_ = value; break;
            case map::kCursorRow: cursorRow_ = value; break;
            case map::kInk: ink_ = value; break;
            case map::kPaper: paper_ = value; break;
            case map::kBorder: border_ = value; break;
            case map::kSound:
                soundLatch_ = value;
                ++notesPlayed_;
                if (onSound && soundEnabled_) {
                    onSound(value);
                }
                break;
            case map::kFrameFlag:
                if (value == 0) {
                    frameFlag_ = false;
                }
                break;
            case map::kScrolled:
                if (value == 0) {
                    scrolled_ = false;
                }
                break;
            case map::kCursorOn:
                cursorOn_ = value != 0;
                break;
            case map::kDiskSectorLow: diskSector_ = (diskSector_ & 0x300) | value; break;
            case map::kDiskSectorHigh:
                diskSector_ = (diskSector_ & 0xFF) | (static_cast<std::uint32_t>(value) << 8);
                break;
            case map::kDiskBufferLow: diskBuffer_ = (diskBuffer_ & 0xFF00) | value; break;
            case map::kDiskBufferHigh:
                diskBuffer_ = static_cast<std::uint16_t>((diskBuffer_ & 0x00FF) | (value << 8));
                break;
            case map::kDiskCommand: executeDiskCommand(value); break;
            case map::kTapeData: tape_.writeByte(value); break;
            case map::kTapeControl:
                switch (value) {
                    case 1: tape_.play(); break;
                    case 2: tape_.record(); break;
                    case 3: tape_.stop(); break;
                    case 4: tape_.rewind(); break;
                    default: break;
                }
                break;
            default: break;
        }
    }
    // ROM, the gaps and the I/O page's unused registers ignore writes. On a
    // machine with no memory management that is the honest behaviour: the byte
    // goes nowhere.
}

void Hb1Machine::executeDiskCommand(std::uint8_t command) {
    diskStatus_ = 0;
    if (!disk_.present()) {
        diskStatus_ = 0x80;
        return;
    }
    switch (command) {
        case 0:
            return;
        case 1: {  // read the sector into RAM
            const std::uint8_t* source = disk_.sector(static_cast<int>(diskSector_));
            if (source == nullptr) {
                diskStatus_ = 0x81;
                return;
            }
            for (int i = 0; i < Hb1Disk::kSectorSize; ++i) {
                const std::uint16_t target =
                    static_cast<std::uint16_t>(diskBuffer_ + i);
                if (target <= map::kRamTop) {
                    ram_[target] = source[i];
                }
            }
            return;
        }
        case 2: {  // write the sector from RAM
            std::uint8_t* target = disk_.sector(static_cast<int>(diskSector_));
            if (target == nullptr) {
                diskStatus_ = 0x81;
                return;
            }
            for (int i = 0; i < Hb1Disk::kSectorSize; ++i) {
                const std::uint16_t source =
                    static_cast<std::uint16_t>(diskBuffer_ + i);
                target[i] = source <= map::kRamTop ? ram_[source] : 0;
            }
            return;
        }
        case 3:  // how many sectors are on the volume
            diskSectorCount_ = static_cast<std::uint16_t>(Hb1Disk::kSectors);
            return;
        default:
            diskStatus_ = 0x82;
            return;
    }
}

void Hb1Machine::tickFrame() {
    ++frameCounter_;
    frameFlag_ = true;
}

void Hb1Machine::runCycles(long cycles) {
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

void Hb1Machine::runFrames(int frames) {
    runCycles(static_cast<long>(frames) * kCyclesPerFrame);
}

void Hb1Machine::type(std::uint8_t key) {
    keyboard_.push(key);
}

void Hb1Machine::typeText(std::string_view text) {
    for (char ch : text) {
        if (ch == '\n') {
            keyboard_.push(static_cast<std::uint8_t>(SpecialKey::Return));
        } else {
            keyboard_.push(static_cast<std::uint8_t>(ch));
        }
    }
}

void Hb1Machine::typeLine(std::string_view text) {
    typeText(text);
    keyboard_.push(static_cast<std::uint8_t>(SpecialKey::Return));
}

void Hb1Machine::pressBreak() {
    cpu_.requestNmi();
}

std::uint8_t Hb1Machine::randomByte() {
    // A 16-bit maximal-length LFSR, the noise generator a machine built down to
    // a price could afford. Deterministic on purpose: `RND` in a test has to
    // answer the same way twice.
    const std::uint16_t bit =
        static_cast<std::uint16_t>(((randomState_ >> 0) ^ (randomState_ >> 2) ^
                                    (randomState_ >> 3) ^ (randomState_ >> 5)) &
                                   1);
    randomState_ = static_cast<std::uint16_t>((randomState_ >> 1) | (bit << 15));
    return static_cast<std::uint8_t>(randomState_ & 0xFF);
}

}  // namespace timewarp::hb1
