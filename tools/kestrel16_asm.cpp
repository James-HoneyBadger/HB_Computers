// SPDX-License-Identifier: MIT
//
// kestrel_asm -- the Kestrel's ROM assembler.
//
// The Kestrel's languages are not tables in this repository's C++ any more: they
// are 6502 machine code, assembled from `roms/kestrel/*.s` at build time into the
// images the machine boots. This is the tool that turns the sources into those
// images, and it is deliberately small -- a two-phase assembler for the
// WDC 65C816 instruction set plus the handful of directives the ROMs need:
//
//   kestrel_asm basic.s --origin $A000 --size 16384 --fill $FF
//          --header basic_rom.hpp --symbols basic.sym --listing basic.lst
//
// Layout is resolved by iterating to a fixed point (rather than the usual single
// two-pass guess), so a label that lands in zero page does not need a hint and
// cannot oscillate: the images are byte-for-byte reproducible.
//
// The dialect:
//
//   LABEL:        a label at the current address
//   NAME = expr   a constant
//         .org $A000        .byte 1,2,'A'      .word LABEL
//         .fill 8,$EA       .res 16           .align 256,$FF
//         .text "ABC"       .include "file.s"
//         LDA #<LABEL       LDA (PTR),Y       BNE LABEL
//
// Expressions are the usual C-ish set over numbers (`$FF`, `%1010`, `123`,
// `'A'`), symbols, `.` (the current address), and the low/high byte operators
// `<` and `>`.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// The instruction set
// ---------------------------------------------------------------------------

enum class Mode {
    Imp,     // implied
    Imp8,    // implied with a signature byte (BRK)
    Acc,
    Imm,     // #expr, one or two bytes depending on .B or .W
    Imm8,    // #expr, always one byte (SEP, REP, COP, WDM)
    Imm16,   // #expr, always two bytes (PEA, PER)
    Dp,      // expr
    DpX,
    DpY,
    DpInd,   // (expr)
    DpIndX,  // (expr,X)
    DpIndY,  // (expr),Y
    DpLong,  // [expr]
    DpLongY, // [expr],Y
    SrS,     // expr,S
    SrSY,    // (expr,S),Y
    Abs,
    AbsX,
    AbsY,
    AbsInd,      // (expr) for JMP
    AbsIndX,     // (expr,X) for JMP and JSR
    AbsIndLong,  // [expr] for JML
    Long,        // $bb:aaaa
    LongX,
    Rel,
    Rel16,
    Blk,         // MVN/MVP: two bank bytes
};

const char* modeName(Mode mode) {
    switch (mode) {
        case Mode::Imp: return "implied";
        case Mode::Imp8: return "implied, signature";
        case Mode::Acc: return "accumulator";
        case Mode::Imm: return "immediate";
        case Mode::Imm8: return "immediate byte";
        case Mode::Imm16: return "immediate word";
        case Mode::Dp: return "direct page";
        case Mode::DpX: return "direct page,X";
        case Mode::DpY: return "direct page,Y";
        case Mode::DpInd: return "(direct page)";
        case Mode::DpIndX: return "(direct page,X)";
        case Mode::DpIndY: return "(direct page),Y";
        case Mode::DpLong: return "[direct page]";
        case Mode::DpLongY: return "[direct page],Y";
        case Mode::SrS: return "stack relative";
        case Mode::SrSY: return "(stack relative),Y";
        case Mode::Abs: return "absolute";
        case Mode::AbsX: return "absolute,X";
        case Mode::AbsY: return "absolute,Y";
        case Mode::AbsInd: return "(absolute)";
        case Mode::AbsIndX: return "(absolute,X)";
        case Mode::AbsIndLong: return "[absolute]";
        case Mode::Long: return "long";
        case Mode::LongX: return "long,X";
        case Mode::Rel: return "relative";
        case Mode::Rel16: return "relative long";
        case Mode::Blk: return "block move";
    }
    return "?";
}

struct OpDef {
    const char* name;
    Mode mode;
    std::uint8_t code;
};

// Every documented opcode. Undocumented opcodes are not assembled: a ROM that
// needs one emits `.byte` and says why in a comment.
constexpr OpDef kOpcodes[] = {
    {"BRK", Mode::Imp8, 0x00},
    {"ORA", Mode::DpIndX, 0x01},
    {"COP", Mode::Imm8, 0x02},
    {"ORA", Mode::SrS, 0x03},
    {"TSB", Mode::Dp, 0x04},
    {"ORA", Mode::Dp, 0x05},
    {"ASL", Mode::Dp, 0x06},
    {"ORA", Mode::DpLong, 0x07},
    {"PHP", Mode::Imp, 0x08},
    {"ORA", Mode::Imm, 0x09},
    {"ASL", Mode::Acc, 0x0A},
    {"PHD", Mode::Imp, 0x0B},
    {"TSB", Mode::Abs, 0x0C},
    {"ORA", Mode::Abs, 0x0D},
    {"ASL", Mode::Abs, 0x0E},
    {"ORA", Mode::Long, 0x0F},
    {"BPL", Mode::Rel, 0x10},
    {"ORA", Mode::DpIndY, 0x11},
    {"ORA", Mode::DpInd, 0x12},
    {"ORA", Mode::SrSY, 0x13},
    {"TRB", Mode::Dp, 0x14},
    {"ORA", Mode::DpX, 0x15},
    {"ASL", Mode::DpX, 0x16},
    {"ORA", Mode::DpLongY, 0x17},
    {"CLC", Mode::Imp, 0x18},
    {"ORA", Mode::AbsY, 0x19},
    {"INC", Mode::Acc, 0x1A},
    {"TCS", Mode::Imp, 0x1B},
    {"TRB", Mode::Abs, 0x1C},
    {"ORA", Mode::AbsX, 0x1D},
    {"ASL", Mode::AbsX, 0x1E},
    {"ORA", Mode::LongX, 0x1F},
    {"JSR", Mode::Abs, 0x20},
    {"AND", Mode::DpIndX, 0x21},
    {"JSL", Mode::Long, 0x22},
    {"AND", Mode::SrS, 0x23},
    {"BIT", Mode::Dp, 0x24},
    {"AND", Mode::Dp, 0x25},
    {"ROL", Mode::Dp, 0x26},
    {"AND", Mode::DpLong, 0x27},
    {"PLP", Mode::Imp, 0x28},
    {"AND", Mode::Imm, 0x29},
    {"ROL", Mode::Acc, 0x2A},
    {"PLD", Mode::Imp, 0x2B},
    {"BIT", Mode::Abs, 0x2C},
    {"AND", Mode::Abs, 0x2D},
    {"ROL", Mode::Abs, 0x2E},
    {"AND", Mode::Long, 0x2F},
    {"BMI", Mode::Rel, 0x30},
    {"AND", Mode::DpIndY, 0x31},
    {"AND", Mode::DpInd, 0x32},
    {"AND", Mode::SrSY, 0x33},
    {"BIT", Mode::DpX, 0x34},
    {"AND", Mode::DpX, 0x35},
    {"ROL", Mode::DpX, 0x36},
    {"AND", Mode::DpLongY, 0x37},
    {"SEC", Mode::Imp, 0x38},
    {"AND", Mode::AbsY, 0x39},
    {"DEC", Mode::Acc, 0x3A},
    {"TSC", Mode::Imp, 0x3B},
    {"BIT", Mode::AbsX, 0x3C},
    {"AND", Mode::AbsX, 0x3D},
    {"ROL", Mode::AbsX, 0x3E},
    {"AND", Mode::LongX, 0x3F},
    {"RTI", Mode::Imp, 0x40},
    {"EOR", Mode::DpIndX, 0x41},
    {"WDM", Mode::Imm8, 0x42},
    {"EOR", Mode::SrS, 0x43},
    {"MVP", Mode::Blk, 0x44},
    {"EOR", Mode::Dp, 0x45},
    {"LSR", Mode::Dp, 0x46},
    {"EOR", Mode::DpLong, 0x47},
    {"PHA", Mode::Imp, 0x48},
    {"EOR", Mode::Imm, 0x49},
    {"LSR", Mode::Acc, 0x4A},
    {"PHK", Mode::Imp, 0x4B},
    {"JMP", Mode::Abs, 0x4C},
    {"EOR", Mode::Abs, 0x4D},
    {"LSR", Mode::Abs, 0x4E},
    {"EOR", Mode::Long, 0x4F},
    {"BVC", Mode::Rel, 0x50},
    {"EOR", Mode::DpIndY, 0x51},
    {"EOR", Mode::DpInd, 0x52},
    {"EOR", Mode::SrSY, 0x53},
    {"MVN", Mode::Blk, 0x54},
    {"EOR", Mode::DpX, 0x55},
    {"LSR", Mode::DpX, 0x56},
    {"EOR", Mode::DpLongY, 0x57},
    {"CLI", Mode::Imp, 0x58},
    {"EOR", Mode::AbsY, 0x59},
    {"PHY", Mode::Imp, 0x5A},
    {"TCD", Mode::Imp, 0x5B},
    {"JML", Mode::Long, 0x5C},
    {"EOR", Mode::AbsX, 0x5D},
    {"LSR", Mode::AbsX, 0x5E},
    {"EOR", Mode::LongX, 0x5F},
    {"RTS", Mode::Imp, 0x60},
    {"ADC", Mode::DpIndX, 0x61},
    {"PER", Mode::Rel16, 0x62},
    {"ADC", Mode::SrS, 0x63},
    {"STZ", Mode::Dp, 0x64},
    {"ADC", Mode::Dp, 0x65},
    {"ROR", Mode::Dp, 0x66},
    {"ADC", Mode::DpLong, 0x67},
    {"PLA", Mode::Imp, 0x68},
    {"ADC", Mode::Imm, 0x69},
    {"ROR", Mode::Acc, 0x6A},
    {"RTL", Mode::Imp, 0x6B},
    {"JMP", Mode::AbsInd, 0x6C},
    {"ADC", Mode::Abs, 0x6D},
    {"ROR", Mode::Abs, 0x6E},
    {"ADC", Mode::Long, 0x6F},
    {"BVS", Mode::Rel, 0x70},
    {"ADC", Mode::DpIndY, 0x71},
    {"ADC", Mode::DpInd, 0x72},
    {"ADC", Mode::SrSY, 0x73},
    {"STZ", Mode::DpX, 0x74},
    {"ADC", Mode::DpX, 0x75},
    {"ROR", Mode::DpX, 0x76},
    {"ADC", Mode::DpLongY, 0x77},
    {"SEI", Mode::Imp, 0x78},
    {"ADC", Mode::AbsY, 0x79},
    {"PLY", Mode::Imp, 0x7A},
    {"TDC", Mode::Imp, 0x7B},
    {"JMP", Mode::AbsIndX, 0x7C},
    {"ADC", Mode::AbsX, 0x7D},
    {"ROR", Mode::AbsX, 0x7E},
    {"ADC", Mode::LongX, 0x7F},
    {"BRA", Mode::Rel, 0x80},
    {"STA", Mode::DpIndX, 0x81},
    {"BRL", Mode::Rel16, 0x82},
    {"STA", Mode::SrS, 0x83},
    {"STY", Mode::Dp, 0x84},
    {"STA", Mode::Dp, 0x85},
    {"STX", Mode::Dp, 0x86},
    {"STA", Mode::DpLong, 0x87},
    {"DEY", Mode::Imp, 0x88},
    {"BIT", Mode::Imm, 0x89},
    {"TXA", Mode::Imp, 0x8A},
    {"PHB", Mode::Imp, 0x8B},
    {"STY", Mode::Abs, 0x8C},
    {"STA", Mode::Abs, 0x8D},
    {"STX", Mode::Abs, 0x8E},
    {"STA", Mode::Long, 0x8F},
    {"BCC", Mode::Rel, 0x90},
    {"STA", Mode::DpIndY, 0x91},
    {"STA", Mode::DpInd, 0x92},
    {"STA", Mode::SrSY, 0x93},
    {"STY", Mode::DpX, 0x94},
    {"STA", Mode::DpX, 0x95},
    {"STX", Mode::DpY, 0x96},
    {"STA", Mode::DpLongY, 0x97},
    {"TYA", Mode::Imp, 0x98},
    {"STA", Mode::AbsY, 0x99},
    {"TXS", Mode::Imp, 0x9A},
    {"TXY", Mode::Imp, 0x9B},
    {"STZ", Mode::Abs, 0x9C},
    {"STA", Mode::AbsX, 0x9D},
    {"STZ", Mode::AbsX, 0x9E},
    {"STA", Mode::LongX, 0x9F},
    {"LDY", Mode::Imm, 0xA0},
    {"LDA", Mode::DpIndX, 0xA1},
    {"LDX", Mode::Imm, 0xA2},
    {"LDA", Mode::SrS, 0xA3},
    {"LDY", Mode::Dp, 0xA4},
    {"LDA", Mode::Dp, 0xA5},
    {"LDX", Mode::Dp, 0xA6},
    {"LDA", Mode::DpLong, 0xA7},
    {"TAY", Mode::Imp, 0xA8},
    {"LDA", Mode::Imm, 0xA9},
    {"TAX", Mode::Imp, 0xAA},
    {"PLB", Mode::Imp, 0xAB},
    {"LDY", Mode::Abs, 0xAC},
    {"LDA", Mode::Abs, 0xAD},
    {"LDX", Mode::Abs, 0xAE},
    {"LDA", Mode::Long, 0xAF},
    {"BCS", Mode::Rel, 0xB0},
    {"LDA", Mode::DpIndY, 0xB1},
    {"LDA", Mode::DpInd, 0xB2},
    {"LDA", Mode::SrSY, 0xB3},
    {"LDY", Mode::DpX, 0xB4},
    {"LDA", Mode::DpX, 0xB5},
    {"LDX", Mode::DpY, 0xB6},
    {"LDA", Mode::DpLongY, 0xB7},
    {"CLV", Mode::Imp, 0xB8},
    {"LDA", Mode::AbsY, 0xB9},
    {"TSX", Mode::Imp, 0xBA},
    {"TYX", Mode::Imp, 0xBB},
    {"LDY", Mode::AbsX, 0xBC},
    {"LDA", Mode::AbsX, 0xBD},
    {"LDX", Mode::AbsY, 0xBE},
    {"LDA", Mode::LongX, 0xBF},
    {"CPY", Mode::Imm, 0xC0},
    {"CMP", Mode::DpIndX, 0xC1},
    {"REP", Mode::Imm8, 0xC2},
    {"CMP", Mode::SrS, 0xC3},
    {"CPY", Mode::Dp, 0xC4},
    {"CMP", Mode::Dp, 0xC5},
    {"DEC", Mode::Dp, 0xC6},
    {"CMP", Mode::DpLong, 0xC7},
    {"INY", Mode::Imp, 0xC8},
    {"CMP", Mode::Imm, 0xC9},
    {"DEX", Mode::Imp, 0xCA},
    {"WAI", Mode::Imp, 0xCB},
    {"CPY", Mode::Abs, 0xCC},
    {"CMP", Mode::Abs, 0xCD},
    {"DEC", Mode::Abs, 0xCE},
    {"CMP", Mode::Long, 0xCF},
    {"BNE", Mode::Rel, 0xD0},
    {"CMP", Mode::DpIndY, 0xD1},
    {"CMP", Mode::DpInd, 0xD2},
    {"CMP", Mode::SrSY, 0xD3},
    {"PEI", Mode::DpInd, 0xD4},
    {"CMP", Mode::DpX, 0xD5},
    {"DEC", Mode::DpX, 0xD6},
    {"CMP", Mode::DpLongY, 0xD7},
    {"CLD", Mode::Imp, 0xD8},
    {"CMP", Mode::AbsY, 0xD9},
    {"PHX", Mode::Imp, 0xDA},
    {"STP", Mode::Imp, 0xDB},
    {"JML", Mode::AbsIndLong, 0xDC},
    {"CMP", Mode::AbsX, 0xDD},
    {"DEC", Mode::AbsX, 0xDE},
    {"CMP", Mode::LongX, 0xDF},
    {"CPX", Mode::Imm, 0xE0},
    {"SBC", Mode::DpIndX, 0xE1},
    {"SEP", Mode::Imm8, 0xE2},
    {"SBC", Mode::SrS, 0xE3},
    {"CPX", Mode::Dp, 0xE4},
    {"SBC", Mode::Dp, 0xE5},
    {"INC", Mode::Dp, 0xE6},
    {"SBC", Mode::DpLong, 0xE7},
    {"INX", Mode::Imp, 0xE8},
    {"SBC", Mode::Imm, 0xE9},
    {"NOP", Mode::Imp, 0xEA},
    {"XBA", Mode::Imp, 0xEB},
    {"CPX", Mode::Abs, 0xEC},
    {"SBC", Mode::Abs, 0xED},
    {"INC", Mode::Abs, 0xEE},
    {"SBC", Mode::Long, 0xEF},
    {"BEQ", Mode::Rel, 0xF0},
    {"SBC", Mode::DpIndY, 0xF1},
    {"SBC", Mode::DpInd, 0xF2},
    {"SBC", Mode::SrSY, 0xF3},
    {"PEA", Mode::Imm16, 0xF4},
    {"SBC", Mode::DpX, 0xF5},
    {"INC", Mode::DpX, 0xF6},
    {"SBC", Mode::DpLongY, 0xF7},
    {"SED", Mode::Imp, 0xF8},
    {"SBC", Mode::AbsY, 0xF9},
    {"PLX", Mode::Imp, 0xFA},
    {"XCE", Mode::Imp, 0xFB},
    {"JSR", Mode::AbsIndX, 0xFC},
    {"SBC", Mode::AbsX, 0xFD},
    {"INC", Mode::AbsX, 0xFE},
    {"SBC", Mode::LongX, 0xFF},
};

const OpDef* findOpcode(const std::string& name, Mode mode) {
    for (const OpDef& op : kOpcodes) {
        if (name == op.name && mode == op.mode) {
            return &op;
        }
    }
    return nullptr;
}

bool opSupports(const std::string& name, Mode mode) {
    return findOpcode(name, mode) != nullptr;
}

// `LDA.B`, `LDA.W` and `LDA.L` name the same instruction: the suffix only
// chooses a width, so the mnemonic test has to look at the part before it.
std::string baseMnemonic(const std::string& name) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot + 1 >= name.size()) {
        return name;
    }
    const std::string suffix = name.substr(dot + 1);
    if (suffix == "B" || suffix == "W" || suffix == "L") {
        return name.substr(0, dot);
    }
    return name;
}

void splitSuffix(std::string& head, std::uint8_t& immBytes, bool& longForm) {
    const std::size_t dot = head.rfind('.');
    if (dot == std::string::npos || dot + 1 >= head.size()) {
        return;
    }
    const std::string suffix = head.substr(dot + 1);
    if (suffix == "B") {
        immBytes = 1;
    } else if (suffix == "W") {
        immBytes = 2;
    } else if (suffix == "L") {
        longForm = true;
    } else {
        return;
    }
    head = head.substr(0, dot);
}

/// The instructions whose immediate is an index register's, which this
/// machine keeps sixteen bits wide.
bool isIndexImmediate(const std::string& name) {
    return name == "LDX" || name == "LDY" || name == "CPX" || name == "CPY";
}

bool isMnemonic(const std::string& name) {
    for (const OpDef& op : kOpcodes) {
        if (name == op.name) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

struct Error : std::runtime_error {
    Error(const std::string& file, int line, const std::string& what)
        : std::runtime_error(file + ":" + std::to_string(line) + ": " + what) {}
};

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------

enum class TokKind { Ident, Number, String, Punct };

struct Tok {
    TokKind kind = TokKind::Punct;
    std::string text;
    long long value = 0;
};

bool identStart(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool identChar(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' ||
           ch == '.';
}

std::vector<Tok> lex(const std::string& line, const std::string& file,
                     int lineNumber) {
    std::vector<Tok> out;
    std::size_t i = 0;
    while (i < line.size()) {
        const char ch = line[i];
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            ++i;
            continue;
        }
        if (ch == ';') {
            break;
        }
        if (identStart(ch) ||
            (ch == '.' && i + 1 < line.size() && identStart(line[i + 1]))) {
            const std::size_t start = i;
            ++i;
            while (i < line.size() && identChar(line[i])) {
                ++i;
            }
            std::string text = line.substr(start, i - start);
            std::transform(text.begin(), text.end(), text.begin(), [](char c) {
                return static_cast<char>(
                    std::toupper(static_cast<unsigned char>(c)));
            });
            out.push_back({TokKind::Ident, text, 0});
            continue;
        }
        if (ch == '$' || ch == '%' ||
            std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            const std::size_t start = i;
            long long value = 0;
            if (ch == '$') {
                ++i;
                const std::size_t digits = i;
                while (i < line.size() &&
                       std::isxdigit(static_cast<unsigned char>(line[i])) != 0) {
                    value = value * 16 +
                            (std::isdigit(static_cast<unsigned char>(line[i]))
                                 ? line[i] - '0'
                                 : (std::toupper(static_cast<unsigned char>(
                                        line[i])) -
                                    'A' + 10));
                    ++i;
                }
                if (i == digits) {
                    throw Error(file, lineNumber, "empty hexadecimal number");
                }
            } else if (ch == '%') {
                ++i;
                const std::size_t digits = i;
                while (i < line.size() && (line[i] == '0' || line[i] == '1')) {
                    value = value * 2 + (line[i] - '0');
                    ++i;
                }
                if (i == digits) {
                    throw Error(file, lineNumber, "empty binary number");
                }
            } else if (ch == '0' && i + 1 < line.size() &&
                       (line[i + 1] == 'x' || line[i + 1] == 'X')) {
                i += 2;                 // 0x: the other way of writing hex
                const std::size_t digits = i;
                while (i < line.size() &&
                       std::isxdigit(static_cast<unsigned char>(line[i])) != 0) {
                    value = value * 16 +
                            (std::isdigit(static_cast<unsigned char>(line[i]))
                                 ? line[i] - '0'
                                 : (std::toupper(static_cast<unsigned char>(
                                        line[i])) -
                                    'A' + 10));
                    ++i;
                }
                if (i == digits) {
                    throw Error(file, lineNumber, "empty hexadecimal number");
                }
            } else {
                while (i < line.size() &&
                       std::isdigit(static_cast<unsigned char>(line[i])) != 0) {
                    value = value * 10 + (line[i] - '0');
                    ++i;
                }
                if (i < line.size() && identChar(line[i])) {
                    throw Error(file, lineNumber,
                                "a number cannot run into a letter: " +
                                    line.substr(start, i - start + 1));
                }
            }
            (void)start;
            out.push_back({TokKind::Number, std::string{}, value});
            continue;
        }
        if (ch == '\'') {
            if (i + 2 >= line.size() || line[i + 2] != '\'') {
                throw Error(file, lineNumber, "unterminated character literal");
            }
            out.push_back({TokKind::Number, std::string{},
                           static_cast<unsigned char>(line[i + 1])});
            i += 3;
            continue;
        }
        if (ch == '"') {
            const std::size_t start = ++i;
            while (i < line.size() && line[i] != '"') {
                ++i;
            }
            if (i >= line.size()) {
                throw Error(file, lineNumber, "unterminated string literal");
            }
            out.push_back({TokKind::String, line.substr(start, i - start), 0});
            ++i;
            continue;
        }
        if (ch == '<' && i + 1 < line.size() && line[i + 1] == '<') {
            out.push_back({TokKind::Punct, "<<", 0});
            i += 2;
            continue;
        }
        if (ch == '>' && i + 1 < line.size() && line[i + 1] == '>') {
            out.push_back({TokKind::Punct, ">>", 0});
            i += 2;
            continue;
        }
        if (std::strchr("#(),[]+-*/&|^<>=:.@", ch) != nullptr) {
            out.push_back({TokKind::Punct, std::string(1, ch), 0});
            ++i;
            continue;
        }
        throw Error(file, lineNumber,
                    std::string("unexpected character '") + ch + "'");
    }
    return out;
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

struct SymbolTable {
    std::map<std::string, long long> values;

    [[nodiscard]] std::optional<long long> find(const std::string& name) const {
        const auto it = values.find(name);
        if (it == values.end()) {
            return std::nullopt;
        }
        return it->second;
    }
};

struct ExprNode;
using ExprPtr = std::unique_ptr<ExprNode>;

struct ExprNode {
    enum class Op {
        Number,
        Symbol,
        Here,
        Negate,
        Not,
        Lo,
        Hi,
        Add,
        Sub,
        Mul,
        Div,
        Mod,
        And,
        Or,
        Xor,
        Shl,
        Shr,
    } op = Op::Number;
    long long value = 0;
    std::string symbol;
    ExprPtr left;
    ExprPtr right;
};

class ExprParser {
public:
    ExprParser(const std::vector<Tok>& tokens, std::size_t begin,
               const std::string& file, int line)
        : tokens_(tokens), pos_(begin), file_(file), line_(line) {}

    [[nodiscard]] ExprPtr parse() {
        ExprPtr node = parseBinary(0);
        return node;
    }

    [[nodiscard]] std::size_t position() const { return pos_; }

private:
    [[nodiscard]] bool atEnd() const {
        return pos_ >= tokens_.size() ||
               tokens_[pos_].kind == TokKind::String;
    }

    [[nodiscard]] const Tok* current() const {
        return atEnd() ? nullptr : &tokens_[pos_];
    }

    [[nodiscard]] bool isPunct(std::string_view text) const {
        const Tok* tok = current();
        return tok != nullptr && tok->kind == TokKind::Punct &&
               tok->text == text;
    }

    ExprPtr make(ExprNode::Op op, ExprPtr left, ExprPtr right = nullptr) {
        auto node = std::make_unique<ExprNode>();
        node->op = op;
        node->left = std::move(left);
        node->right = std::move(right);
        return node;
    }

    ExprPtr makeNumber(long long value) {
        auto node = std::make_unique<ExprNode>();
        node->op = ExprNode::Op::Number;
        node->value = value;
        return node;
    }

    struct BinaryOp {
        std::string_view text;
        ExprNode::Op op;
        int precedence;
    };

    static constexpr BinaryOp kBinaryOps[] = {
        {"|", ExprNode::Op::Or, 1},    {"^", ExprNode::Op::Xor, 2},
        {"&", ExprNode::Op::And, 3},   {"<<", ExprNode::Op::Shl, 4},
        {">>", ExprNode::Op::Shr, 4},  {"+", ExprNode::Op::Add, 5},
        {"-", ExprNode::Op::Sub, 5},   {"*", ExprNode::Op::Mul, 6},
        {"/", ExprNode::Op::Div, 6},   {"%", ExprNode::Op::Mod, 6},
    };

    ExprPtr parseBinary(int minPrecedence) {
        ExprPtr left = parseUnary();
        while (!atEnd()) {
            const Tok* tok = current();
            if (tok->kind != TokKind::Punct) {
                break;
            }
            const BinaryOp* found = nullptr;
            for (const BinaryOp& candidate : kBinaryOps) {
                if (candidate.text == tok->text) {
                    found = &candidate;
                    break;
                }
            }
            if (found == nullptr || found->precedence < minPrecedence) {
                break;
            }
            const BinaryOp op = *found;
            ++pos_;
            ExprPtr right = parseBinary(op.precedence + 1);
            left = make(op.op, std::move(left), std::move(right));
        }
        return left;
    }

    ExprPtr parseUnary() {
        if (atEnd()) {
            throw Error(file_, line_, "expected an expression");
        }
        const Tok tok = tokens_[pos_];
        if (tok.kind == TokKind::Punct) {
            if (tok.text == "-") {
                ++pos_;
                return make(ExprNode::Op::Negate, parseUnary());
            }
            if (tok.text == "<") {
                ++pos_;
                return make(ExprNode::Op::Lo, parseUnary());
            }
            if (tok.text == ">") {
                ++pos_;
                return make(ExprNode::Op::Hi, parseUnary());
            }
            if (tok.text == "~") {
                ++pos_;
                return make(ExprNode::Op::Not, parseUnary());
            }
            if (tok.text == "(") {
                ++pos_;
                ExprPtr inner = parseBinary(0);
                if (!isPunct(")")) {
                    throw Error(file_, line_, "expected ')'");
                }
                ++pos_;
                return inner;
            }
            if (tok.text == ".") {
                ++pos_;
                auto node = std::make_unique<ExprNode>();
                node->op = ExprNode::Op::Here;
                return node;
            }
        }
        if (tok.kind == TokKind::Number) {
            ++pos_;
            return makeNumber(tok.value);
        }
        if (tok.kind == TokKind::Ident) {
            ++pos_;
            auto node = std::make_unique<ExprNode>();
            node->op = ExprNode::Op::Symbol;
            node->symbol = tok.text;
            return node;
        }
        throw Error(file_, line_, "unexpected token in expression: " + tok.text);
    }

    const std::vector<Tok>& tokens_;
    std::size_t pos_;
    std::string file_;
    int line_;
};

long long evaluate(const ExprNode& node, const SymbolTable& symbols,
                   long long here, const std::string& file, int line,
                   bool allowUnknown) {
    switch (node.op) {
        case ExprNode::Op::Number:
            return node.value;
        case ExprNode::Op::Here:
            return here;
        case ExprNode::Op::Symbol: {
            const std::optional<long long> value = symbols.find(node.symbol);
            if (!value) {
                if (allowUnknown) {
                    // A forward reference in an early layout pass: the size it
                    // will eventually produce is not known yet, and pretending
                    // it is a byte is how a two-pass assembler gets the wrong
                    // zero-page form. The fixed-point loop settles it.
                    return 0;
                }
                throw Error(file, line, "unknown symbol: " + node.symbol);
            }
            return *value;
        }
        case ExprNode::Op::Negate:
            return -evaluate(*node.left, symbols, here, file, line, allowUnknown);
        case ExprNode::Op::Not:
            return ~evaluate(*node.left, symbols, here, file, line, allowUnknown);
        case ExprNode::Op::Lo:
            return evaluate(*node.left, symbols, here, file, line, allowUnknown) & 0xFF;
        case ExprNode::Op::Hi:
            return (evaluate(*node.left, symbols, here, file, line, allowUnknown) >> 8) & 0xFF;
        default:
            break;
    }
    const long long left = evaluate(*node.left, symbols, here, file, line, allowUnknown);
    const long long right = evaluate(*node.right, symbols, here, file, line, allowUnknown);
    switch (node.op) {
        case ExprNode::Op::Add: return left + right;
        case ExprNode::Op::Sub: return left - right;
        case ExprNode::Op::Mul: return left * right;
        case ExprNode::Op::Div:
            if (right == 0) {
                throw Error(file, line, "division by zero in expression");
            }
            return left / right;
        case ExprNode::Op::Mod:
            if (right == 0) {
                throw Error(file, line, "division by zero in expression");
            }
            return left % right;
        case ExprNode::Op::And: return left & right;
        case ExprNode::Op::Or: return left | right;
        case ExprNode::Op::Xor: return left ^ right;
        case ExprNode::Op::Shl: return left << right;
        case ExprNode::Op::Shr: return left >> right;
        default: break;
    }
    throw Error(file, line, "bad expression");
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

enum class StmtKind {
    Instruction,
    Equate,
    Org,
    Byte,
    Word,
    Fill,
    Reserve,
    Align,
    Text,
    Include,
};

struct Statement {
    StmtKind kind = StmtKind::Instruction;
    std::string label;
    std::string file;
    int line = 0;
    std::string raw;

    // Instruction
    std::string mnemonic;
    Mode mode = Mode::Imp;
    ExprPtr operand;
    // The width of an immediate, which depends on the M or X flag at run time.
    // A byte is the default, as it is on the 6502 this processor starts in;
    // `.W` says the flag is clear and two bytes are wanted, `.B` says so again
    // for a reader, and `.L` asks for a long address.
    std::uint8_t immBytes = 1;
    /// True when the source said `.B` or `.W`, which beats any default.
    bool immExplicit = false;
    // `LDA.L` asks for the 24-bit form even when the value would fit.
    bool longForm = false;

    // Data directives
    std::vector<ExprPtr> values;
    ExprPtr fillValue;
    long long count = 0;
    std::string text;
};

bool isDirective(const std::string& name) {
    return name == ".ORG" || name == ".BYTE" || name == ".DB" ||
           name == ".WORD" || name == ".DW" || name == ".FILL" ||
           name == ".RES" || name == ".ALIGN" || name == ".TEXT" ||
           name == ".INCLUDE" || name == ".ASCIIZ";
}

class SourceParser {
public:
    explicit SourceParser(int depth = 0) : depth_(depth) {}

    std::vector<Statement> parseFile(const std::string& path) {
        if (depth_ > 8) {
            throw Error(path, 0, "include nesting is too deep");
        }
        std::ifstream in(path);
        if (!in) {
            throw Error(path, 0, "cannot open source file");
        }
        std::vector<Statement> statements;
        std::string line;
        int number = 0;
        while (std::getline(in, line)) {
            ++number;
            appendLine(path, number, line, statements);
        }
        return statements;
    }

private:
    void appendLine(const std::string& file, int number,
                    const std::string& raw, std::vector<Statement>& out) {
        const std::vector<Tok> tokens = lex(raw, file, number);
        if (tokens.empty()) {
            return;
        }
        std::size_t pos = 0;
        std::string label;
        if (tokens.size() >= 2 && tokens[0].kind == TokKind::Ident &&
            tokens[1].kind == TokKind::Punct && tokens[1].text == ":") {
            label = tokens[0].text;
            pos = 2;
        } else if (tokens.size() >= 2 && tokens[0].kind == TokKind::Ident &&
                   tokens[1].kind == TokKind::Punct &&
                   tokens[1].text == "=") {
            Statement stmt;
            stmt.kind = StmtKind::Equate;
            stmt.label = tokens[0].text;
            stmt.file = file;
            stmt.line = number;
            ExprParser parser(tokens, 2, file, number);
            stmt.operand = parser.parse();
            stmt.raw = raw;
            out.push_back(std::move(stmt));
            return;
        }
        if (label.empty() && tokens[0].kind == TokKind::Ident &&
            !isDirective(tokens[0].text) &&
            !isMnemonic(baseMnemonic(tokens[0].text))) {
            // The column-0 form: `LOOP  DEX`. An identifier in the label field
            // is a label whether or not it carries the colon.
            label = tokens[0].text;
            pos = 1;
        }
        if (pos >= tokens.size()) {
            if (!label.empty()) {
                Statement stmt;
                stmt.kind = StmtKind::Org;
                stmt.label = label;
                stmt.file = file;
                stmt.line = number;
                stmt.operand = nullptr;
                stmt.raw = raw;
                out.push_back(std::move(stmt));
                return;
            }
            return;
        }
        if (tokens[pos].kind != TokKind::Ident) {
            throw Error(file, number, "expected a label, directive or mnemonic");
        }
        std::string head = tokens[pos].text;
        std::uint8_t immBytes = 1;
        bool longForm = false;
        const bool suffixed = head.find('.') != std::string::npos;
        splitSuffix(head, immBytes, longForm);
        if (isDirective(head)) {
            Statement directive =
                parseDirective(head, label, tokens, pos + 1, file, number);
            directive.raw = raw;
            out.push_back(std::move(directive));
            return;
        }
        Statement stmt;
        stmt.kind = StmtKind::Instruction;
        stmt.label = label;
        stmt.file = file;
        stmt.line = number;
        stmt.mnemonic = head;
        stmt.immBytes = immBytes;
        stmt.immExplicit = suffixed;
        stmt.longForm = longForm;
        if (!isMnemonic(head)) {
            throw Error(file, number, "unknown mnemonic: " + head);
        }
        parseOperand(stmt, tokens, pos + 1, file, number);
        stmt.raw = raw;
        out.push_back(std::move(stmt));
    }

    static bool looksLikeIndex(const std::vector<Tok>& tokens, std::size_t pos,
                               std::string_view reg) {
        // `,X` / `,Y` at `pos`
        return pos < tokens.size() && pos + 1 < tokens.size() &&
               tokens[pos].kind == TokKind::Punct &&
               tokens[pos].text == "," && tokens[pos + 1].kind == TokKind::Ident &&
               tokens[pos + 1].text == reg;
    }

    void parseOperand(Statement& stmt, const std::vector<Tok>& tokens,
                      std::size_t pos, const std::string& file, int line) {
        const std::string& name = stmt.mnemonic;
        if (pos >= tokens.size()) {
            // `ASL` with nothing after it shifts the accumulator: the implied
            // form does not exist for the shift mnemonics. REP and SEP look
            // implied but always carry a one-byte mask.
            if (opSupports(name, Mode::Acc)) {
                stmt.mode = Mode::Acc;
            } else if (opSupports(name, Mode::Imp8)) {
                stmt.mode = Mode::Imp8;
            } else {
                stmt.mode = Mode::Imp;
            }
            return;
        }
        if (opSupports(name, Mode::Blk) && name != "BLK") {
            // MVN and MVP take two banks: `MVN dstbank,srcbank`.
            std::size_t comma = tokens.size();
            int depth = 0;
            for (std::size_t i = pos; i < tokens.size(); ++i) {
                if (tokens[i].kind != TokKind::Punct) {
                    continue;
                }
                const std::string& t = tokens[i].text;
                if (t == "(" || t == "[") {
                    ++depth;
                } else if (t == ")" || t == "]") {
                    --depth;
                } else if (t == "," && depth == 0) {
                    comma = i;
                    break;
                }
            }
            if (comma == tokens.size()) {
                throw Error(file, line, name + " needs two bank operands");
            }
            ExprPtr dst = parseExprRange(tokens, pos, comma, file, line);
            ExprPtr src = parseExprRange(tokens, comma + 1, tokens.size(), file,
                                         line);
            auto scale = std::make_unique<ExprNode>();
            scale->op = ExprNode::Op::Number;
            scale->value = 256;
            auto high = std::make_unique<ExprNode>();
            high->op = ExprNode::Op::Mul;
            high->left = std::move(dst);
            high->right = std::move(scale);
            auto total = std::make_unique<ExprNode>();
            total->op = ExprNode::Op::Add;
            total->left = std::move(high);
            total->right = std::move(src);
            stmt.operand = std::move(total);
            stmt.mode = Mode::Blk;
            return;
        }
        if (tokens[pos].kind == TokKind::Ident && tokens[pos].text == "A" &&
            pos + 1 == tokens.size() && opSupports(name, Mode::Acc)) {
            stmt.mode = Mode::Acc;
            return;
        }
        if (tokens[pos].kind == TokKind::Punct && tokens[pos].text == "#") {
            ExprParser parser(tokens, pos + 1, file, line);
            stmt.operand = parser.parse();
            if (opSupports(name, Mode::Imm)) {
                stmt.mode = Mode::Imm;
                if (!stmt.immExplicit && isIndexImmediate(name)) {
                    // X and Y are sixteen bits wide on this machine's own
                    // conventions, and every widening instruction says `.B`.
                    stmt.immBytes = 2;
                }
            } else if (opSupports(name, Mode::Imm16)) {
                stmt.mode = Mode::Imm16;
            } else if (opSupports(name, Mode::Imm8)) {
                stmt.mode = Mode::Imm8;
            } else {
                stmt.mode = Mode::Imp8;  // REP, SEP, BRK, COP, WDM
            }
            return;
        }
        if (tokens[pos].kind == TokKind::Punct && tokens[pos].text == "[") {
            const std::size_t after = matchClose(tokens, pos, "[", "]", file, line);
            const bool indexedY = looksLikeIndex(tokens, after, "Y");
            stmt.operand = parseExprRange(tokens, pos + 1, after - 1, file, line);
            stmt.mode = indexedY ? Mode::DpLongY
                                 : pickMode(name, Mode::DpLong, Mode::AbsIndLong);
            return;
        }
        if (tokens[pos].kind == TokKind::Punct && tokens[pos].text == "(") {
            const std::size_t after = matchClose(tokens, pos, "(", ")", file, line);
            const std::size_t close = after - 1;
            std::size_t exprEnd = close;
            bool indexedX = false;
            bool stackRel = false;
            if (close >= pos + 4 && tokens[close - 2].kind == TokKind::Punct &&
                tokens[close - 2].text == "," &&
                tokens[close - 1].kind == TokKind::Ident) {
                const std::string& reg = tokens[close - 1].text;
                if (reg == "X") {
                    indexedX = true;
                    exprEnd = close - 2;
                } else if (reg == "S") {
                    stackRel = true;
                    exprEnd = close - 2;
                }
            }
            stmt.operand = parseExprRange(tokens, pos + 1, exprEnd, file, line);
            if (indexedX) {
                stmt.mode = pickMode(name, Mode::DpIndX, Mode::AbsIndX);
                if (!opSupports(name, stmt.mode) &&
                    opSupports(name, Mode::AbsIndLong)) {
                    stmt.mode = Mode::AbsIndLong;
                }
                return;
            }
            if (stackRel) {
                if (!looksLikeIndex(tokens, after, "Y")) {
                    throw Error(file, line,
                                "(expr,S) must be followed by ,Y on the 65816");
                }
                stmt.mode = Mode::SrSY;
                return;
            }
            if (looksLikeIndex(tokens, after, "Y")) {
                // There is no absolute form of (dp),Y on the 65816.
                stmt.mode = Mode::DpIndY;
                return;
            }
            stmt.mode = pickMode(name, Mode::DpInd, Mode::AbsInd);
            return;
        }
        // Plain expression, optionally `,X`, `,Y` or `,S`.
        std::size_t exprEnd = tokens.size();
        bool indexedX = false;
        bool indexedY = false;
        bool stackRel = false;
        if (looksLikeIndex(tokens, tokens.size() - 2, "X")) {
            indexedX = true;
            exprEnd = tokens.size() - 2;
        } else if (looksLikeIndex(tokens, tokens.size() - 2, "Y")) {
            indexedY = true;
            exprEnd = tokens.size() - 2;
        } else if (looksLikeIndex(tokens, tokens.size() - 2, "S")) {
            stackRel = true;
            exprEnd = tokens.size() - 2;
        }
        stmt.operand = parseExprRange(tokens, pos, exprEnd, file, line);
        if (indexedX) {
            stmt.mode = opSupports(name, Mode::DpX)   ? Mode::DpX
                        : opSupports(name, Mode::AbsX) ? Mode::AbsX
                                                       : Mode::LongX;
            return;
        }
        if (indexedY) {
            stmt.mode = opSupports(name, Mode::DpY) ? Mode::DpY : Mode::AbsY;
            return;
        }
        if (stackRel) {
            if (!opSupports(name, Mode::SrS)) {
                throw Error(file, line, name + " does not support ,S");
            }
            stmt.mode = Mode::SrS;
            return;
        }
        if (opSupports(name, Mode::Rel)) {
            stmt.mode = Mode::Rel;
            return;
        }
        if (opSupports(name, Mode::Rel16)) {
            stmt.mode = Mode::Rel16;
            return;
        }
        if (opSupports(name, Mode::Dp)) {
            stmt.mode = Mode::Dp;
        } else if (opSupports(name, Mode::Abs)) {
            stmt.mode = Mode::Abs;
        } else if (opSupports(name, Mode::Long)) {
            stmt.mode = Mode::Long;
        } else {
            // PEA is the only instruction whose bare operand is a sixteen-bit
            // immediate rather than an address.
            stmt.mode = Mode::Imm16;
        }
    }

    // The mode that a mnemonic can actually encode, preferring the short form
    // because the emitter widens it when the value does not fit.
    static Mode pickMode(const std::string& name, Mode narrow, Mode wide) {
        return opSupports(name, narrow) ? narrow : wide;
    }

    static std::size_t matchClose(const std::vector<Tok>& tokens,
                                  std::size_t pos, std::string_view open,
                                  std::string_view close,
                                  const std::string& file, int line) {
        int depth = 0;
        for (std::size_t i = pos; i < tokens.size(); ++i) {
            if (tokens[i].kind != TokKind::Punct) {
                continue;
            }
            if (tokens[i].text == open) {
                ++depth;
            } else if (tokens[i].text == close) {
                if (--depth == 0) {
                    return i + 1;
                }
            }
        }
        throw Error(file, line,
                    "unbalanced " + std::string(open) + std::string(close));
        return tokens.size();
    }

    // An operand expression, with `$bb:aaaa` shorthand for a bank-qualified
    // address. The bank and the offset are combined as bank * 65536 + offset,
    // so the value is 24-bit and the emitter picks a long addressing mode.
    static ExprPtr parseExprRange(const std::vector<Tok>& tokens,
                                  std::size_t begin, std::size_t end,
                                  const std::string& file, int line) {
        if (end < begin) {
            throw Error(file, line, "expected an address");
        }
        int depth = 0;
        for (std::size_t i = begin; i < end; ++i) {
            if (tokens[i].kind != TokKind::Punct) {
                continue;
            }
            const std::string& t = tokens[i].text;
            if (t == "(" || t == "[") {
                ++depth;
            } else if (t == ")" || t == "]") {
                --depth;
            } else if (t == ":" && depth == 0) {
                ExprPtr bank = parsePlain(tokens, begin, i, file, line);
                ExprPtr offset = parsePlain(tokens, i + 1, end, file, line);
                auto shift = std::make_unique<ExprNode>();
                shift->op = ExprNode::Op::Number;
                shift->value = 65536;
                auto scaled = std::make_unique<ExprNode>();
                scaled->op = ExprNode::Op::Mul;
                scaled->left = std::move(bank);
                scaled->right = std::move(shift);
                auto total = std::make_unique<ExprNode>();
                total->op = ExprNode::Op::Add;
                total->left = std::move(scaled);
                total->right = std::move(offset);
                return total;
            }
        }
        return parsePlain(tokens, begin, end, file, line);
    }

    static ExprPtr parsePlain(const std::vector<Tok>& tokens, std::size_t begin,
                              std::size_t end, const std::string& file,
                              int line) {
        std::vector<Tok> slice(tokens.begin() + static_cast<long>(begin),
                               tokens.begin() + static_cast<long>(end));
        ExprParser parser(slice, 0, file, line);
        return parser.parse();
    }

    Statement parseDirective(const std::string& name, const std::string& label,
                             const std::vector<Tok>& tokens, std::size_t pos,
                             const std::string& file, int line) {
        Statement stmt;
        stmt.label = label;
        stmt.file = file;
        stmt.line = line;
        if (name == ".ORG") {
            stmt.kind = StmtKind::Org;
            ExprParser parser(tokens, pos, file, line);
            stmt.operand = parser.parse();
            return stmt;
        }
        if (name == ".BYTE" || name == ".DB" || name == ".WORD" ||
            name == ".DW") {
            stmt.kind = (name == ".BYTE" || name == ".DB") ? StmtKind::Byte
                                                           : StmtKind::Word;
            std::size_t at = pos;
            while (at < tokens.size()) {
                ExprParser parser(tokens, at, file, line);
                stmt.values.push_back(parser.parse());
                at = parser.position();
                if (at < tokens.size() && tokens[at].kind == TokKind::Punct &&
                    tokens[at].text == ",") {
                    ++at;
                    continue;
                }
                break;
            }
            return stmt;
        }
        if (name == ".FILL") {
            stmt.kind = StmtKind::Fill;
            ExprParser countParser(tokens, pos, file, line);
            stmt.operand = countParser.parse();
            std::size_t at = countParser.position();
            if (at >= tokens.size() || tokens[at].kind != TokKind::Punct ||
                tokens[at].text != ",") {
                throw Error(file, line, ".fill needs a count and a byte");
            }
            ExprParser valueParser(tokens, at + 1, file, line);
            stmt.fillValue = valueParser.parse();
            return stmt;
        }
        if (name == ".RES") {
            stmt.kind = StmtKind::Reserve;
            ExprParser parser(tokens, pos, file, line);
            stmt.operand = parser.parse();
            return stmt;
        }
        if (name == ".ALIGN") {
            stmt.kind = StmtKind::Align;
            ExprParser parser(tokens, pos, file, line);
            stmt.operand = parser.parse();
            std::size_t at = parser.position();
            if (at < tokens.size() && tokens[at].kind == TokKind::Punct &&
                tokens[at].text == ",") {
                ExprParser valueParser(tokens, at + 1, file, line);
                stmt.fillValue = valueParser.parse();
            }
            return stmt;
        }
        if (name == ".TEXT" || name == ".ASCIIZ") {
            stmt.kind = StmtKind::Text;
            if (pos >= tokens.size() || tokens[pos].kind != TokKind::String) {
                throw Error(file, line, name + " needs a quoted string");
            }
            stmt.text = tokens[pos].text;
            if (name == ".ASCIIZ") {
                stmt.text.push_back('\0');
            }
            return stmt;
        }
        if (name == ".INCLUDE") {
            stmt.kind = StmtKind::Include;
            if (pos >= tokens.size() || tokens[pos].kind != TokKind::String) {
                throw Error(file, line, ".include needs a quoted path");
            }
            stmt.text = tokens[pos].text;
            return stmt;
        }
        throw Error(file, line, "unknown directive: " + name);
    }

    int depth_ = 0;
};

// ---------------------------------------------------------------------------
// Assembly
// ---------------------------------------------------------------------------

struct ListingEntry {
    std::uint32_t address = 0;
    std::uint32_t end = 0;
    std::string file;
    int line = 0;
    std::string text;
};

struct Assembly {
    std::vector<std::uint8_t> image;
    std::map<std::string, long long> symbols;
    std::vector<ListingEntry> listing;
};

struct Layout {
    std::uint32_t pc = 0;
    std::map<std::string, long long> symbols;
    std::map<std::uint32_t, std::uint8_t> image;
    std::vector<ListingEntry> listing;
};

struct AssemblerOptions {
    std::uint32_t origin = 0;
    std::uint32_t size = 0;
    int fill = 0xFF;
};

class Assembler {
public:
    Assembler(std::vector<Statement> statements, AssemblerOptions options,
              std::string entryName)
        : statements_(std::move(statements)),
          options_(options),
          entryName_(std::move(entryName)) {}

    Assembly assemble() {
        Layout previous;
        for (int iteration = 0; iteration < 16; ++iteration) {
            Layout current;
            runPass(previous, current, false, false);
            if (iteration > 0 && current.symbols == previous.symbols) {
                Layout final;
                runPass(current, final, true, true);
                Assembly result;
                result.image = finish(final);
                result.symbols = final.symbols;
                result.listing = final.listing;
                return result;
            }
            previous = std::move(current);
        }
        throw Error(entryName_, 0,
                    "layout did not converge -- a label is flipping between zero "
                    "page and absolute addressing");
    }

private:
    void runPass(const Layout& previous, Layout& current, bool emit,
                 bool strict) {
        current.pc = options_.origin;
        for (const Statement& stmt : statements_) {
            const std::uint32_t start = current.pc;
            if (!stmt.label.empty() && stmt.kind != StmtKind::Equate &&
                stmt.kind != StmtKind::Org) {
                define(current, stmt, start);
            } else if (!stmt.label.empty() && stmt.kind == StmtKind::Org &&
                       stmt.operand == nullptr) {
                define(current, stmt, start);
            }
            switch (stmt.kind) {
                case StmtKind::Instruction:
                    assembleInstruction(stmt, previous, current, emit, strict);
                    break;
                case StmtKind::Equate: {
                    const long long value =
                        valueOf(*stmt.operand, stmt, previous, current, strict);
                    current.symbols[stmt.label] = value;
                    break;
                }
                case StmtKind::Org: {
                    if (stmt.operand == nullptr) {
                        break;
                    }
                    const long long where =
                        valueOf(*stmt.operand, stmt, previous, current, strict);
                    if (where < 0 || where > 0xFFFFFF) {
                        throw Error(stmt.file, stmt.line,
                                    ".org outside the 16MB address space");
                    }
                    if (static_cast<std::uint32_t>(where) < current.pc) {
                        throw Error(stmt.file, stmt.line,
                                    ".org moves backwards");
                    }
                    current.pc = static_cast<std::uint32_t>(where);
                    break;
                }
                case StmtKind::Byte:
                    for (const ExprPtr& expr : stmt.values) {
                        const long long v =
                            valueOf(*expr, stmt, previous, current, strict);
                        if (v < -128 || v > 255) {
                            throw Error(stmt.file, stmt.line,
                                        ".byte value does not fit in a byte");
                        }
                        emitByte(current, stmt, emit,
                                 static_cast<std::uint8_t>(v & 0xFF));
                    }
                    break;
                case StmtKind::Word:
                    for (const ExprPtr& expr : stmt.values) {
                        const long long v =
                            valueOf(*expr, stmt, previous, current, strict);
                        if (v < -32768 || v > 0xFFFF) {
                            throw Error(stmt.file, stmt.line,
                                        ".word value does not fit in a word");
                        }
                        emitByte(current, stmt, emit,
                                 static_cast<std::uint8_t>(v & 0xFF));
                        emitByte(current, stmt, emit,
                                 static_cast<std::uint8_t>((v >> 8) & 0xFF));
                    }
                    break;
                case StmtKind::Fill:
                case StmtKind::Reserve:
                case StmtKind::Align: {
                    long count = 0;
                    std::uint8_t fillByte = 0;
                    if (stmt.kind == StmtKind::Fill) {
                        count = valueOf(*stmt.operand, stmt, previous, current, strict);
                        fillByte = static_cast<std::uint8_t>(
                            valueOf(*stmt.fillValue, stmt, previous, current, strict) &
                            0xFF);
                    } else if (stmt.kind == StmtKind::Reserve) {
                        count = valueOf(*stmt.operand, stmt, previous, current, strict);
                    } else {
                        const long long alignment =
                            valueOf(*stmt.operand, stmt, previous, current, strict);
                        if (alignment <= 0) {
                            throw Error(stmt.file, stmt.line,
                                        ".align needs a positive boundary");
                        }
                        const long long remainder =
                            static_cast<long long>(current.pc) % alignment;
                        count = remainder == 0 ? 0 : alignment - remainder;
                        if (stmt.fillValue != nullptr) {
                            fillByte = static_cast<std::uint8_t>(
                                valueOf(*stmt.fillValue, stmt, previous,
                                        current, strict) &
                                0xFF);
                        }
                    }
                    if (count < 0) {
                        throw Error(stmt.file, stmt.line,
                                    "negative fill or reserve count");
                    }
                    for (long i = 0; i < count; ++i) {
                        emitByte(current, stmt, emit, fillByte);
                    }
                    break;
                }
                case StmtKind::Text:
                    for (char ch : stmt.text) {
                        emitByte(current, stmt, emit,
                                 static_cast<std::uint8_t>(
                                     static_cast<unsigned char>(ch)));
                    }
                    break;
                case StmtKind::Include:
                    break;
            }
            if (emit) {
                current.listing.push_back(
                    {start, current.pc, stmt.file, stmt.line, stmt.raw});
            }
        }
    }

    void define(Layout& layout, const Statement& stmt, std::uint32_t address) {
        if (layout.symbols.find(stmt.label) != layout.symbols.end()) {
            throw Error(stmt.file, stmt.line,
                        "label defined twice: " + stmt.label);
        }
        layout.symbols[stmt.label] = address;
    }

    long long valueOf(const ExprNode& node, const Statement& stmt,
                      const Layout& previous, const Layout& current,
                      bool strict) const {
        SymbolTable table;
        table.values = previous.symbols;
        for (const auto& [name, value] : current.symbols) {
            table.values[name] = value;
        }
        return evaluate(node, table, current.pc, stmt.file, stmt.line, !strict);
    }

    void emitByte(Layout& layout, const Statement& stmt, bool emit,
                  std::uint8_t value) {
        const std::uint32_t address = layout.pc;
        if (options_.size != 0) {
            if (address < options_.origin ||
                address >= options_.origin + options_.size) {
                throw Error(stmt.file, stmt.line,
                            "code falls outside the ROM window $" +
                                hex(options_.origin) + "..=$" +
                                hex(options_.origin + options_.size - 1));
            }
        }
        if (address > 0xFFFFFF) {
            throw Error(stmt.file, stmt.line, "past the end of memory");
        }
        if (emit) {
            layout.image[address] = value;
        }
        ++layout.pc;
    }

    void assembleInstruction(const Statement& stmt, const Layout& previous,
                             Layout& current, bool emit, bool strict) {
        Mode mode = stmt.mode;
        long long value = 0;
        if (stmt.operand != nullptr) {
            value = valueOf(*stmt.operand, stmt, previous, current, strict);
        }
        // An operand that will not fit its short form is widened as far as the
        // mnemonic allows: direct page to absolute to long.
        if (stmt.longForm) {
            if ((mode == Mode::Dp || mode == Mode::Abs) &&
                opSupports(stmt.mnemonic, Mode::Long)) {
                mode = Mode::Long;
            } else if ((mode == Mode::DpX || mode == Mode::AbsX) &&
                       opSupports(stmt.mnemonic, Mode::LongX)) {
                mode = Mode::LongX;
            }
        }
        const bool tooWide = value < 0 || value > 0xFF;
        const bool pastBank = value < 0 || value > 0xFFFF;
        if (mode == Mode::DpX && tooWide && opSupports(stmt.mnemonic, Mode::AbsX)) {
            mode = Mode::AbsX;
        } else if (mode == Mode::DpY && tooWide &&
                   opSupports(stmt.mnemonic, Mode::AbsY)) {
            mode = Mode::AbsY;
        } else if (mode == Mode::Dp && tooWide &&
                   opSupports(stmt.mnemonic, Mode::Abs)) {
            mode = Mode::Abs;
        } else if (mode == Mode::DpLong && tooWide &&
                   opSupports(stmt.mnemonic, Mode::AbsIndLong)) {
            mode = Mode::AbsIndLong;
        } else if (mode == Mode::DpInd && tooWide &&
                   opSupports(stmt.mnemonic, Mode::AbsInd)) {
            mode = Mode::AbsInd;
        }
        if ((mode == Mode::Abs || mode == Mode::AbsX) && pastBank) {
            if (mode == Mode::Abs && opSupports(stmt.mnemonic, Mode::Long)) {
                mode = Mode::Long;
            } else if (mode == Mode::AbsX &&
                       opSupports(stmt.mnemonic, Mode::LongX)) {
                mode = Mode::LongX;
            }
        }
        const OpDef* op = findOpcode(stmt.mnemonic, mode);
        if (op == nullptr) {
            throw Error(stmt.file, stmt.line,
                        stmt.mnemonic + " does not support " + modeName(mode));
        }
        const std::uint32_t at = current.pc;
        (void)strict;
        if (mode == Mode::Rel) {
            // A branch that cannot reach its target becomes its own inverse
            // branch over a JMP. The pair of branch opcodes differs in bit 5
            // (BCC $90 / BCS $B0, BNE $D0 / BEQ $F0, and so on), so the
            // inverse is one XOR -- which is what makes a long branch possible
            // without a macro or a rewrite of the source.
            const long long offset = value - static_cast<long long>(at + 2);
            if (offset >= -128 && offset <= 127) {
                emitByte(current, stmt, emit, op->code);
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>(offset & 0xFF));
            } else {
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>(op->code ^ 0x20));
                emitByte(current, stmt, emit, 3);
                emitByte(current, stmt, emit, 0x4C);
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>(value & 0xFF));
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>((value >> 8) & 0xFF));
            }
            return;
        }
        if (mode == Mode::Rel16) {
            const long long offset = value - static_cast<long long>(at + 3);
            emitByte(current, stmt, emit, op->code);
            emitByte(current, stmt, emit,
                     static_cast<std::uint8_t>(offset & 0xFF));
            emitByte(current, stmt, emit,
                     static_cast<std::uint8_t>((offset >> 8) & 0xFF));
            return;
        }
        emitByte(current, stmt, emit, op->code);
        switch (mode) {
            case Mode::Imp:
            case Mode::Acc:
                return;
            case Mode::Imp8:
            case Mode::Imm8:
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>(value & 0xFF));
                return;
            case Mode::Imm16:
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>(value & 0xFF));
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>((value >> 8) & 0xFF));
                return;
            case Mode::Imm:
                if (stmt.immBytes == 1 && (value < -128 || value > 0xFF)) {
                    // A byte immediate that does not fit is a missing `.W`:
                    // the assembler cannot see the processor's M flag, so the
                    // source has to say when two bytes are meant.
                    throw Error(stmt.file, stmt.line,
                                stmt.mnemonic +
                                    ": immediate does not fit a byte; use .W");
                }
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>(value & 0xFF));
                if (stmt.immBytes == 1) {
                    return;
                }
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>((value >> 8) & 0xFF));
                return;
            case Mode::Dp:
            case Mode::DpX:
            case Mode::DpY:
            case Mode::DpInd:
            case Mode::DpIndX:
            case Mode::DpIndY:
            case Mode::DpLong:
            case Mode::DpLongY:
            case Mode::SrS:
            case Mode::SrSY:
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>(value & 0xFF));
                return;
            case Mode::Blk:
                // The value is dst * 256 + src, and the opcode wants the
                // destination bank first.
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>((value >> 8) & 0xFF));
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>(value & 0xFF));
                return;
            case Mode::Abs:
            case Mode::AbsX:
            case Mode::AbsY:
            case Mode::AbsInd:
            case Mode::AbsIndX:
            case Mode::AbsIndLong:
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>(value & 0xFF));
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>((value >> 8) & 0xFF));
                return;
            case Mode::Long:
            case Mode::LongX:
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>(value & 0xFF));
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>((value >> 8) & 0xFF));
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>((value >> 16) & 0xFF));
                return;
            default:
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>(value & 0xFF));
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>((value >> 8) & 0xFF));
                return;
        }
    }

    std::vector<std::uint8_t> finish(const Layout& layout) const {
        const std::uint32_t base =
            options_.size != 0
                ? options_.origin
                : (layout.image.empty() ? options_.origin
                                        : layout.image.begin()->first);
        if (options_.size == 0) {
            if (layout.image.empty()) {
                return {};
            }
            const std::uint32_t last = layout.image.rbegin()->first;
            std::vector<std::uint8_t> out(
                last - base + 1, static_cast<std::uint8_t>(options_.fill));
            for (const auto& [address, byte] : layout.image) {
                out[address - base] = byte;
            }
            return out;
        }
        std::vector<std::uint8_t> out(options_.size,
                                      static_cast<std::uint8_t>(options_.fill));
        for (const auto& [address, byte] : layout.image) {
            if (address < base || address >= base + options_.size) {
                continue;
            }
            out[address - base] = byte;
        }
        return out;
    }

    static std::string hex(std::uint32_t value) {
        char buffer[16];
        std::snprintf(buffer, sizeof buffer, "%04X", value);
        return buffer;
    }

    std::vector<Statement> statements_;
    AssemblerOptions options_;
    std::string entryName_;
};

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

void writeAll(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot write " + path);
    }
    out << text;
}

/// Expands `.include` in place, so the layout passes see one flat program.
/// The included file's own `.org`/labels do the rest: a ROM is a single image,
/// so an include is textual rather than a separate link.
void expandIncludes(std::vector<Statement>& statements, int depth = 0) {
    if (depth > 8) {
        throw std::runtime_error("include nesting is too deep");
    }
    std::vector<Statement> out;
    for (Statement& stmt : statements) {
        if (stmt.kind != StmtKind::Include) {
            out.push_back(std::move(stmt));
            continue;
        }
        SourceParser parser(depth + 1);
        std::vector<Statement> included = parser.parseFile(stmt.text);
        expandIncludes(included, depth + 1);
        for (Statement& inner : included) {
            out.push_back(std::move(inner));
        }
    }
    statements = std::move(out);
}

std::string headerText(const std::string& name, std::uint32_t origin,
                       const std::vector<std::uint8_t>& image) {
    std::string upper;
    for (char ch : name) {
        upper.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch))));
    }
    std::ostringstream out;
    out << "// SPDX-License-Identifier: MIT\n";
    out << "//\n";
    out << "// Generated by kestrel16_asm from roms/kestrel16/" << name
        << ".s -- do not edit.\n";
    out << "//\n";
    out << "// The " << name << " ROM: " << image.size()
        << " bytes of 65816 machine code,\n";
    out << "// assembled at $" << std::hex << std::uppercase << origin
        << std::dec << ", a bank of its own.\n\n";
    out << "#pragma once\n\n";
    out << "#include <array>\n";
    out << "#include <cstdint>\n\n";
    out << "namespace kestrel::model1992::rom {\n\n";
    out << "inline constexpr std::uint32_t k" << upper << "Origin = 0x"
        << std::hex << std::uppercase << origin << std::dec << ";\n";
    out << "inline constexpr std::array<std::uint8_t, " << image.size()
        << "> k" << upper << "Image = {";
    for (std::size_t i = 0; i < image.size(); ++i) {
        if (i % 16 == 0) {
            out << "\n    ";
        }
        out << "0x" << std::hex << std::uppercase << std::setw(2)
            << std::setfill('0') << static_cast<int>(image[i]) << std::dec
            << std::setfill(' ') << ",";
    }
    out << "\n};\n\n";
    out << "}  // namespace kestrel::model1992::rom\n";
    return out.str();
}

std::uint32_t parseNumber(const std::string& text) {
    if (text.empty()) {
        throw std::runtime_error("empty number");
    }
    if (text[0] == '$') {
        return static_cast<std::uint32_t>(std::stoul(text.substr(1), nullptr, 16));
    }
    return static_cast<std::uint32_t>(std::stoul(text, nullptr, 0));
}

std::string symbolText(const std::map<std::string, long long>& symbols) {
    std::vector<std::pair<long long, std::string>> ordered;
    ordered.reserve(symbols.size());
    for (const auto& [name, value] : symbols) {
        ordered.emplace_back(value, name);
    }
    std::sort(ordered.begin(), ordered.end());
    std::ostringstream out;
    for (const auto& [value, name] : ordered) {
        out << std::hex << std::uppercase << std::setw(4)
            << std::setfill('0') << (value & 0xFFFF) << std::dec
            << std::setfill(' ') << " " << name << "\n";
    }
    return out.str();
}

std::string listingText(const std::vector<ListingEntry>& listing,
                        const std::vector<std::uint8_t>& image,
                        std::uint32_t base) {
    std::ostringstream out;
    for (const ListingEntry& entry : listing) {
        out << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
            << entry.address << std::dec << std::setfill(' ') << "  ";
        std::string bytes;
        for (std::uint32_t address = entry.address; address < entry.end;
             ++address) {
            const std::uint32_t index = address - base;
            if (index >= image.size()) {
                break;
            }
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "%02X ",
                          static_cast<unsigned>(image[index]));
            bytes += buffer;
        }
        while (bytes.size() < 12) {
            bytes.push_back(' ');
        }
        out << bytes << "  " << entry.text << "\n";
    }
    return out.str();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::string source;
        std::string headerPath;
        std::string symbolsPath;
        std::string binaryPath;
        std::string listingPath;
        std::string entryName;
        AssemblerOptions options;

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto next = [&]() -> std::string {
                if (i + 1 >= argc) {
                    throw std::runtime_error("missing value for " + arg);
                }
                return argv[++i];
            };
            if (arg == "--origin") {
                options.origin = parseNumber(next());
            } else if (arg == "--size") {
                options.size = parseNumber(next());
            } else if (arg == "--fill") {
                options.fill = static_cast<int>(parseNumber(next()));
            } else if (arg == "--header") {
                headerPath = next();
            } else if (arg == "--symbols") {
                symbolsPath = next();
            } else if (arg == "--bin") {
                binaryPath = next();
            } else if (arg == "--listing") {
                listingPath = next();
            } else if (arg == "--name") {
                entryName = next();
            } else if (arg == "--help" || arg == "-h") {
                std::printf(
                    "usage: kestrel_asm <source.s> [--name basic] [--origin $A000]\n"
                    "              [--size 16384] [--fill $FF]\n"
                    "              [--header out.hpp] [--bin out.bin]\n"
                    "              [--symbols out.sym] [--listing out.lst]\n");
                return 0;
            } else if (!arg.empty() && arg[0] == '-') {
                throw std::runtime_error("unknown option " + arg);
            } else {
                source = arg;
            }
        }
        if (source.empty()) {
            std::fprintf(stderr, "kestrel_asm: no source file\n");
            return 2;
        }
        if (entryName.empty()) {
            entryName = source;
            const std::size_t slash = entryName.find_last_of('/');
            if (slash != std::string::npos) {
                entryName = entryName.substr(slash + 1);
            }
            const std::size_t dot = entryName.find('.');
            if (dot != std::string::npos) {
                entryName = entryName.substr(0, dot);
            }
        }

        SourceParser parser;
        std::vector<Statement> statements = parser.parseFile(source);
        expandIncludes(statements);

        Assembler assembler(std::move(statements), options, entryName);
        const Assembly assembly = assembler.assemble();

        if (!binaryPath.empty()) {
            std::ofstream out(binaryPath, std::ios::binary);
            if (!out) {
                throw std::runtime_error("cannot write " + binaryPath);
            }
            out.write(reinterpret_cast<const char*>(assembly.image.data()),
                      static_cast<std::streamsize>(assembly.image.size()));
        }
        if (!headerPath.empty()) {
            writeAll(headerPath,
                     headerText(entryName, options.origin, assembly.image));
        }
        if (!symbolsPath.empty()) {
            writeAll(symbolsPath, symbolText(assembly.symbols));
        }
        if (!listingPath.empty()) {
            writeAll(listingPath,
                     listingText(assembly.listing, assembly.image,
                                 options.origin));
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "kestrel_asm: %s\n", error.what());
        return 1;
    }
}
