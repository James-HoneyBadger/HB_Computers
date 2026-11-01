// SPDX-License-Identifier: MIT
//
// The machine: the profile, the decode, the I/O page, the frame and the disk
// commands -- everything that is the same for both models once the numbers are
// different.
//
// Three things in here are worth reading before changing anything:
//
//   * the profile. makeProfile is the only place a model's clock, memory sizes,
//     screen geometry, ROM windows and disk geometry are written down. A new
//     difference between the machines belongs there, not in a `if (model)`.
//   * the decode. read() and write() answer in a fixed order, and the order is
//     significant where regions abut: on the Kestrel, RAM is checked before the
//     screen because they are neighbours; on the Kestrel 16 the KERNAL is checked
//     before RAM because it stands at the top of bank zero.
//   * the registers. layOutRegisters maps an offset in the I/O page to a device,
//     and readRegister/writeRegister are the only code that knows what a device
//     does. A read is an input and a write is an output; the page is not storage.
//
// The machine's bus. Every address the processor puts on the wire is decoded here
// against the profile of the model the machine was built as: RAM, the two ROM
// windows, the cartridge, the tube's two arrays and the I/O page.

#include "kestrel/machine.hpp"

#include <algorithm>
#include <cstring>

#include "kestrel/basic_rom.hpp"
#include "kestrel/kernal_rom.hpp"
#include "kestrel16/basic_rom.hpp"
#include "kestrel16/kernal_rom.hpp"

namespace kestrel {

namespace {

Profile makeProfile(Model model) {
    if (model == Model::Kestrel) {
        return Profile{Model::Kestrel, "Kestrel",
                       1'000'000, 50, 20'000,
                       0x8000,
                       0x8000, 40, 25, 0x8400,
                       0x8800, 0x0100,
                       0x9000, 0x1000,
                       0xE000, 8192,
                       0xA000, 16384,
                       512, 256, 16};
    }
    return Profile{Model::Kestrel16, "Kestrel 16",
                   8'000'000, 50, 160'000,
                   0x100000,
                   0x8000, 80, 50, 0x8FA0,
                   0xA000, 0x0100,
                   0xC000, 0x20000,
                   0xE000, 8192,
                   0x010000, 65536,
                   4096, 512, 32};
}

}  // namespace

const Profile& profileOf(Model model) {
    static const Profile kestrel = makeProfile(Model::Kestrel);
    static const Profile kestrel16 = makeProfile(Model::Kestrel16);
    return model == Model::Kestrel ? kestrel : kestrel16;
}

Machine::Machine(Model model)
    : profile_(profileOf(model)),
      ram_(static_cast<std::size_t>(profile_.ramBytes), 0),
      video_(profile_.columns, profile_.rows),
      keyboard_(profile_.keyboardCapacity),
      disk_(profile_.diskSectors, profile_.diskSectorSize),
      ink_(profile_.model == Model::Kestrel ? 0x07 : 0x07),
      paper_(0x00),
      border_(0x09) {
    layOutRegisters();
    mountShippedRoms();
    reset();
}

void Machine::layOutRegisters() {
    registers_.fill(Reg::None);
    const auto at = [this](std::uint16_t offset, Reg kind) {
        if (offset < registers_.size()) {
            registers_[offset] = kind;
        }
    };

    // The low registers are the same on both machines: a keyboard, a cursor, the
    // colours, the sound latch and the frame.
    at(0x00, Reg::KeyboardData);
    at(0x01, Reg::KeyboardStatus);
    at(0x02, Reg::KeyboardClear);
    at(0x03, Reg::CursorColumn);
    at(0x04, Reg::CursorRow);
    at(0x05, Reg::Ink);
    at(0x06, Reg::Paper);
    at(0x07, Reg::Border);
    at(0x08, Reg::Sound);
    at(0x09, Reg::FrameLow);
    at(0x0A, Reg::FrameHigh);
    at(0x0B, Reg::FrameFlag);
    at(0x0C, Reg::DiskSectorLow);
    at(0x0D, Reg::DiskSectorHigh);
    at(0x0E, Reg::DiskCommand);
    at(0x0F, Reg::DiskStatus);
    at(0x10, Reg::DiskAddressLow);
    at(0x11, Reg::DiskAddressHigh);
    if (profile_.model == Model::Kestrel) {
        // A 1982 disk controller: a sixteen-bit buffer pointer and a query
        // register that reports the volume's size.
        at(0x12, Reg::DiskCountLow);
        at(0x13, Reg::DiskCountHigh);
        at(0x14, Reg::Random);
        at(0x15, Reg::LastKey);
        at(0x16, Reg::Scrolled);
        at(0x17, Reg::CursorOn);
        at(0x18, Reg::TapeData);
        at(0x19, Reg::TapeStatus);
        at(0x1A, Reg::TapeControl);
        return;
    }
    // A 1992 one: the buffer pointer is a full twenty-four-bit address, so the
    // registers after it move along by one.
    at(0x12, Reg::DiskAddressBank);
    at(0x13, Reg::DiskCountLow);
    at(0x14, Reg::DiskCountHigh);
    at(0x15, Reg::Random);
    at(0x16, Reg::LastKey);
    at(0x17, Reg::Scrolled);
    at(0x18, Reg::CursorOn);
    at(0x19, Reg::TapeData);
    at(0x1A, Reg::TapeStatus);
    at(0x1B, Reg::TapeControl);
}

// Power-on's ROMs: the pair the build assembled for this model. They are mounted
// as images rather than linked into the machine's code, so the same `Machine`
// class runs whichever ROMs a host hands it.
void Machine::mountShippedRoms() {
    if (profile_.model == Model::Kestrel) {
        mountKernal(std::vector<std::uint8_t>(std::begin(kestrel::model1982::rom::kKERNALImage),
                                              std::end(kestrel::model1982::rom::kKERNALImage)),
                    "kernal");
        mountBasic(std::vector<std::uint8_t>(std::begin(kestrel::model1982::rom::kBASICImage),
                                             std::end(kestrel::model1982::rom::kBASICImage)),
                   "basic");
        return;
    }
    mountKernal(std::vector<std::uint8_t>(std::begin(kestrel::model1992::rom::kKERNALImage),
                                          std::end(kestrel::model1992::rom::kKERNALImage)),
                "kernal");
    mountBasic(std::vector<std::uint8_t>(std::begin(kestrel::model1992::rom::kBASICImage),
                                         std::end(kestrel::model1992::rom::kBASICImage)),
               "basic");
}

int Machine::cyclesPerSecond() const noexcept {
    switch (speed_) {
    case Speed::Mhz1: return 1'000'000;
    case Speed::Mhz2: return 2'000'000;
    case Speed::Mhz4: return 4'000'000;
    case Speed::Mhz8: return 8'000'000;
    case Speed::Turbo: return 0;
    }
    return profile_.clockHz;
}

bool Machine::mountKernal(const std::vector<std::uint8_t>& image,
                          std::string_view id) {
    if (static_cast<int>(image.size()) != profile_.kernalSize) {
        return false;
    }
    kernal_ = image;
    kernalId_ = id;
    return true;
}

bool Machine::mountBasic(const std::vector<std::uint8_t>& image,
                         std::string_view id) {
    if (static_cast<int>(image.size()) != profile_.basicSize) {
        return false;
    }
    basic_ = image;
    basicId_ = id;
    return true;
}

bool Machine::mountCartridge(const std::vector<std::uint8_t>& image,
                             std::string_view id) {
    if (static_cast<int>(image.size()) != profile_.cartridgeSize) {
        return false;
    }
    cartridge_ = image;
    cartridgeId_ = id;
    cartridgeMounted_ = true;
    return true;
}

void Machine::ejectCartridge() noexcept {
    cartridge_.clear();
    cartridgeId_.clear();
    cartridgeMounted_ = false;
}

// A cold boot: RAM and the tube cleared, the devices idle, the clock at zero. The
// ROMs are not touched -- they are read-only and the machine is powered on with
// them already in.
void Machine::reset() {
    std::fill(ram_.begin(), ram_.end(), 0);
    video_.powerOn(ink_, paper_);
    keyboard_.clear();
    cursorColumn_ = 0;
    cursorRow_ = 0;
    cursorOn_ = profile_.model == Model::Kestrel;
    scrolled_ = false;
    frameCounter_ = 0;
    frameFlag_ = false;
    cyclesToFrame_ = 0;
    soundLatch_ = 0;
    notesPlayed_ = 0;
    randomState_ = 0xACE1;
    lastKey_ = 0;
    diskSector_ = 0;
    diskAddress_ = 0;
    // The Kestrel's count register reports the volume's size, so it answers from
    // the moment the machine is switched on.
    diskCount_ = profile_.model == Model::Kestrel && disk_.present()
                     ? static_cast<std::uint16_t>(profile_.diskSectors)
                     : 0;
    diskStatus_ = 0;
    if (profile_.model == Model::Kestrel) {
        cpu6502_.reset();
    } else {
        cpu65816_.reset();
    }
}

std::uint8_t Machine::ramAt(std::uint32_t address) const {
    return address < ram_.size() ? ram_[address] : 0;
}

void Machine::setRamAt(std::uint32_t address, std::uint8_t value) {
    if (address < ram_.size()) {
        ram_[address] = value;
    }
}

std::uint8_t Machine::readRegister(Reg kind) {
    switch (kind) {
    case Reg::KeyboardData: {
        const std::uint8_t key = keyboard_.read();
        if (key != 0) {
            lastKey_ = key;
        }
        return key;
    }
    case Reg::KeyboardStatus:
        return keyboard_.available()
                   ? static_cast<std::uint8_t>(profile_.model == Model::Kestrel ? 0xFF : 0x80)
                   : 0x00;
    case Reg::CursorColumn: return cursorColumn_;
    case Reg::CursorRow: return cursorRow_;
    case Reg::Ink: return ink_;
    case Reg::Paper: return paper_;
    case Reg::Border: return border_;
    case Reg::Sound: return 0;
    case Reg::FrameLow:
        return static_cast<std::uint8_t>(frameCounter_ & 0xFF);
    case Reg::FrameHigh:
        return static_cast<std::uint8_t>(frameCounter_ >> 8);
    case Reg::FrameFlag:
        return frameFlag_
                   ? static_cast<std::uint8_t>(profile_.model == Model::Kestrel ? 0xFF : 0x80)
                   : 0x00;
    case Reg::DiskStatus: return diskStatus_;
    case Reg::DiskCountLow:
        return static_cast<std::uint8_t>(diskCount_ & 0xFF);
    case Reg::DiskCountHigh:
        return static_cast<std::uint8_t>(diskCount_ >> 8);
    case Reg::Random: return randomByte();
    case Reg::LastKey: return lastKey_;
    case Reg::Scrolled:
        return scrolled_
                   ? static_cast<std::uint8_t>(profile_.model == Model::Kestrel ? 0xFF : 0x80)
                   : 0x00;
    case Reg::CursorOn:
        return cursorOn_
                   ? static_cast<std::uint8_t>(profile_.model == Model::Kestrel ? 0x01 : 0x80)
                   : 0x00;
    case Reg::TapeData: return tape_.readByte();
    case Reg::TapeStatus: return tape_.status();
    default: return 0;
    }
}

void Machine::writeRegister(Reg kind, std::uint8_t value) {
    switch (kind) {
    case Reg::KeyboardClear: keyboard_.clear(); break;
    case Reg::CursorColumn:
        cursorColumn_ = value;
        if (cursorColumn_ >= profile_.columns) {
            cursorColumn_ = static_cast<std::uint8_t>(profile_.columns - 1);
        }
        break;
    case Reg::CursorRow:
        cursorRow_ = value;
        if (cursorRow_ >= profile_.rows) {
            cursorRow_ = static_cast<std::uint8_t>(profile_.rows - 1);
        }
        break;
    case Reg::Ink: ink_ = value; break;
    case Reg::Paper: paper_ = value; break;
    case Reg::Border: border_ = value; break;
    case Reg::Sound:
        soundLatch_ = value;
        ++notesPlayed_;
        if (onSound && soundEnabled_) {
            onSound(value);
        }
        break;
    case Reg::FrameFlag:
        if (value == 0) {
            frameFlag_ = false;
        }
        break;
    case Reg::Scrolled:
        if (value == 0) {
            scrolled_ = false;
        }
        break;
    case Reg::CursorOn: cursorOn_ = value != 0; break;
    case Reg::DiskSectorLow:
        diskSector_ = (diskSector_ & 0xFFFFFF00u) | value;
        break;
    case Reg::DiskSectorHigh:
        diskSector_ = (diskSector_ & 0xFFFF00FFu) |
                      (static_cast<std::uint32_t>(value) << 8);
        break;
    case Reg::DiskAddressLow:
        diskAddress_ = (diskAddress_ & 0xFFFF00u) | value;
        break;
    case Reg::DiskAddressHigh:
        diskAddress_ = (diskAddress_ & 0xFF00FFu) |
                       (static_cast<std::uint32_t>(value) << 8);
        break;
    case Reg::DiskAddressBank:
        diskAddress_ = (diskAddress_ & 0x00FFFFu) |
                       (static_cast<std::uint32_t>(value) << 16);
        break;
    case Reg::DiskCountLow:
        if (profile_.model == Model::Kestrel) {
            break;  // the Kestrel's count register is the volume's answer
        }
        diskCount_ = static_cast<std::uint16_t>((diskCount_ & 0xFF00) | value);
        break;
    case Reg::DiskCountHigh:
        if (profile_.model == Model::Kestrel) {
            break;
        }
        diskCount_ = static_cast<std::uint16_t>((diskCount_ & 0x00FF) |
                                                (value << 8));
        break;
    case Reg::DiskCommand: executeDiskCommand(value); break;
    case Reg::TapeData: tape_.writeByte(value); break;
    case Reg::TapeControl:
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

std::uint8_t Machine::read(std::uint32_t address) {
    const std::uint8_t bank = static_cast<std::uint8_t>(address >> 16);
    const auto offset = static_cast<std::uint16_t>(address & 0xFFFF);

    if (profile_.model == Model::Kestrel) {
        const std::uint16_t at = offset;
        if (at < profile_.ramBytes) {
            return ram_[at];
        }
        const auto cellsEnd = static_cast<std::uint16_t>(profile_.screenBase +
                                                         profile_.columns * profile_.rows);
        if (at >= profile_.screenBase && at < cellsEnd) {
            return video_.cell(at - profile_.screenBase);
        }
        if (at >= cellsEnd && at < profile_.attributeBase) {
            return static_cast<std::uint8_t>(' ');  // the padding is screen too
        }
        const auto attributesEnd = static_cast<std::uint16_t>(
            profile_.attributeBase + profile_.columns * profile_.rows);
        if (at >= profile_.attributeBase && at < attributesEnd) {
            return video_.attribute(at - profile_.attributeBase);
        }
        if (at >= profile_.ioBase && at < profile_.ioBase + profile_.ioSize) {
            return readRegister(registers_[at - profile_.ioBase]);
        }
        if (at >= profile_.cartridgeBase &&
            at < profile_.cartridgeBase + profile_.cartridgeSize) {
            if (!cartridgeMounted_) {
                return 0;
            }
            return cartridge_[at - profile_.cartridgeBase];
        }
        if (at >= profile_.basicBase &&
            at < profile_.basicBase + profile_.basicSize) {
            const auto index = static_cast<std::size_t>(at - profile_.basicBase);
            return index < basic_.size() ? basic_[index] : 0xFF;
        }
        if (at >= profile_.kernalBase) {
            const auto index = static_cast<std::size_t>(at - profile_.kernalBase);
            return index < kernal_.size() ? kernal_[index] : 0xFF;
        }
        return 0;
    }

    if (bank == 0) {
        if (offset >= profile_.kernalBase) {
            const auto index = static_cast<std::size_t>(offset - profile_.kernalBase);
            return index < kernal_.size() ? kernal_[index] : 0xFF;
        }
        if (offset >= profile_.cartridgeBase &&
            offset < profile_.cartridgeBase + 0x2000) {
            if (!cartridgeMounted_) {
                return 0;
            }
            return cartridge_[offset - profile_.cartridgeBase];
        }
        const auto cellsEnd = static_cast<std::uint16_t>(profile_.screenBase +
                                                         profile_.columns * profile_.rows);
        if (offset >= profile_.screenBase && offset < cellsEnd) {
            return video_.cell(offset - profile_.screenBase);
        }
        const auto attributesEnd = static_cast<std::uint16_t>(
            profile_.attributeBase + profile_.columns * profile_.rows);
        if (offset >= profile_.attributeBase && offset < attributesEnd) {
            return video_.attribute(offset - profile_.attributeBase);
        }
        if (offset >= profile_.ioBase && offset < profile_.ioBase + profile_.ioSize) {
            return readRegister(registers_[offset - profile_.ioBase]);
        }
        if (offset >= profile_.ioBase) {
            return 0;
        }
        return ram_[address];
    }
    if (bank == (profile_.basicBase >> 16)) {
        return offset < basic_.size() ? basic_[offset] : 0xFF;
    }
    if (bank >= 0x04 && bank <= 0x07 && cartridgeMounted_) {
        const auto index = static_cast<std::size_t>(bank - 0x04) * 0x10000 + offset;
        return index < cartridge_.size() ? cartridge_[index] : 0xFF;
    }
    if (address < ram_.size()) {
        return ram_[address];
    }
    return 0;
}

void Machine::write(std::uint32_t address, std::uint8_t value) {
    const std::uint8_t bank = static_cast<std::uint8_t>(address >> 16);
    const auto offset = static_cast<std::uint16_t>(address & 0xFFFF);

    if (profile_.model == Model::Kestrel) {
        const std::uint16_t at = offset;
        if (at < profile_.ramBytes) {
            ram_[at] = value;
            return;
        }
        const auto cellsEnd = static_cast<std::uint16_t>(profile_.screenBase +
                                                         profile_.columns * profile_.rows);
        if (at >= profile_.screenBase && at < cellsEnd) {
            video_.setCell(at - profile_.screenBase, value);
            return;
        }
        const auto attributesEnd = static_cast<std::uint16_t>(
            profile_.attributeBase + profile_.columns * profile_.rows);
        if (at >= profile_.attributeBase && at < attributesEnd) {
            video_.setAttribute(at - profile_.attributeBase, value);
            return;
        }
        if (at >= profile_.ioBase && at < profile_.ioBase + profile_.ioSize) {
            writeRegister(registers_[at - profile_.ioBase], value);
        }
        return;  // ROM, the gaps and the unused registers go nowhere
    }

    if (bank == 0) {
        if (offset >= profile_.kernalBase) {
            return;
        }
        if (offset >= profile_.cartridgeBase &&
            offset < profile_.cartridgeBase + 0x2000) {
            return;
        }
        const auto cellsEnd = static_cast<std::uint16_t>(profile_.screenBase +
                                                         profile_.columns * profile_.rows);
        if (offset >= profile_.screenBase && offset < cellsEnd) {
            video_.setCell(offset - profile_.screenBase, value);
            return;
        }
        const auto attributesEnd = static_cast<std::uint16_t>(
            profile_.attributeBase + profile_.columns * profile_.rows);
        if (offset >= profile_.attributeBase && offset < attributesEnd) {
            video_.setAttribute(offset - profile_.attributeBase, value);
            return;
        }
        if (offset >= profile_.ioBase && offset < profile_.ioBase + profile_.ioSize) {
            writeRegister(registers_[offset - profile_.ioBase], value);
            return;
        }
        if (offset >= profile_.ioBase) {
            return;
        }
        ram_[address] = value;
        return;
    }
    if (bank == 1) {
        return;  // the language ROM
    }
    if (bank >= 0x04 && bank <= 0x07) {
        return;
    }
    if (address < ram_.size()) {
        ram_[address] = value;
    }
}

void Machine::executeDiskCommand(std::uint8_t command) {
    if (profile_.model == Model::Kestrel) {
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
            for (int i = 0; i < profile_.diskSectorSize; ++i) {
                const auto target = static_cast<std::uint16_t>(
                    diskAddress_ + static_cast<std::uint32_t>(i));
                if (target <= profile_.ramBytes - 1) {
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
            for (int i = 0; i < profile_.diskSectorSize; ++i) {
                const auto source = static_cast<std::uint16_t>(
                    diskAddress_ + static_cast<std::uint32_t>(i));
                target[i] = source <= profile_.ramBytes - 1 ? ram_[source] : 0;
            }
            return;
        }
        case 3:  // how many sectors are on the volume
            diskCount_ = static_cast<std::uint16_t>(profile_.diskSectors);
            return;
        default:
            diskStatus_ = 0x82;
            return;
        }
    }

    diskStatus_ = disk_.present() ? 0x04 : 0;  // present
    if (!disk_.present()) {
        diskStatus_ |= 0x02;  // error
        return;
    }
    const std::uint16_t count = diskCount_ == 0 ? 1 : diskCount_;
    switch (command) {
    case 1:   // read
    case 2: { // write
        std::vector<std::uint8_t> buffer(static_cast<std::size_t>(profile_.diskSectorSize), 0);
        std::uint32_t address = diskAddress_;
        for (std::uint16_t i = 0; i < count; ++i) {
            const std::uint32_t sector = diskSector_ + i;
            if (command == 1) {
                if (!disk_.readSector(sector, buffer.data())) {
                    diskStatus_ |= 0x02;
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
                    diskStatus_ |= 0x02;
                    return;
                }
            }
            address += static_cast<std::uint32_t>(profile_.diskSectorSize);
        }
        break;
    }
    case 3:  // format
        disk_.format();
        break;
    case 4:  // query
        break;
    default:
        diskStatus_ |= 0x02;
        return;
    }
    diskStatus_ |= 0x80;  // done
}

std::uint8_t Machine::randomByte() {
    // A maximal-length shift register: cheap, and repeatable, which is the only
    // property a test needs.
    const auto bit = static_cast<std::uint16_t>((randomState_ ^ (randomState_ >> 1)) & 1);
    randomState_ = static_cast<std::uint16_t>((randomState_ >> 1) | (bit << 15));
    return static_cast<std::uint8_t>(randomState_ & 0xFF);
}

// One frame of the machine's own time. The counter and the flag are all the
// machine does: the interrupt is the line the ROM sees, and the work -- blinking
// the cursor, draining the keyboard -- is the KERNAL's handler.
void Machine::tickFrame() {
    ++frameCounter_;
    frameFlag_ = true;
}

// Run the processor, and count frames as its cycles pass. The interrupt line is
// recomputed before every instruction because it is level-triggered: it is high
// while a key waits or a frame has passed, and it drops when the ROM has dealt
// with both.
void Machine::runCycles(long cycles) {
    long done = 0;
    while (done < cycles) {
        int taken = 0;
        if (profile_.model == Model::Kestrel) {
            cpu6502_.setIrqLine(interruptPending());
            taken = cpu6502_.step();
        } else {
            cpu65816_.setIrqLine(interruptPending());
            taken = cpu65816_.step();
        }
        done += taken;
        cyclesToFrame_ += static_cast<std::uint32_t>(taken);
        if (cyclesToFrame_ >= static_cast<std::uint32_t>(profile_.cyclesPerFrame)) {
            cyclesToFrame_ -= static_cast<std::uint32_t>(profile_.cyclesPerFrame);
            tickFrame();
        }
    }
}

void Machine::runFrames(int frames) {
    runCycles(static_cast<long>(frames) * profile_.cyclesPerFrame);
}

void Machine::type(std::uint8_t key) {
    keyboard_.push(key);
}

void Machine::typeText(std::string_view text) {
    for (const char ch : text) {
        keyboard_.push(ch == '\n' ? static_cast<std::uint8_t>(SpecialKey::Return)
                                  : static_cast<std::uint8_t>(ch));
    }
}

void Machine::typeLine(std::string_view text) {
    typeText(text);
    keyboard_.push(static_cast<std::uint8_t>(SpecialKey::Return));
}

void Machine::pressBreak() {
    if (profile_.model == Model::Kestrel) {
        cpu6502_.requestNmi();
    } else {
        cpu65816_.requestNmi();
    }
}

}  // namespace kestrel
