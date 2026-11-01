// SPDX-License-Identifier: MIT
//
// kestrel_asm -- the Kestrel's ROM assembler.
//
// The Kestrel's languages are not tables in this repository's C++ any more: they
// are 6502 machine code, assembled from `roms/kestrel/*.s` at build time into the
// images the machine boots. This is the tool that turns the sources into those
// images, and it is deliberately small -- a two-phase assembler for the
// documented 6502 instruction set plus the handful of directives the ROMs need:
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
    Imp,
    Acc,
    Imm,
    Zp,
    ZpX,
    ZpY,
    Abs,
    AbsX,
    AbsY,
    Ind,
    IndX,
    IndY,
    Rel,
};

const char* modeName(Mode mode) {
    switch (mode) {
        case Mode::Imp: return "implied";
        case Mode::Acc: return "accumulator";
        case Mode::Imm: return "immediate";
        case Mode::Zp: return "zero page";
        case Mode::ZpX: return "zero page,X";
        case Mode::ZpY: return "zero page,Y";
        case Mode::Abs: return "absolute";
        case Mode::AbsX: return "absolute,X";
        case Mode::AbsY: return "absolute,Y";
        case Mode::Ind: return "indirect";
        case Mode::IndX: return "(indirect,X)";
        case Mode::IndY: return "(indirect),Y";
        case Mode::Rel: return "relative";
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
    {"ADC", Mode::Imm, 0x69},  {"ADC", Mode::Zp, 0x65},
    {"ADC", Mode::ZpX, 0x75},  {"ADC", Mode::Abs, 0x6D},
    {"ADC", Mode::AbsX, 0x7D}, {"ADC", Mode::AbsY, 0x79},
    {"ADC", Mode::IndX, 0x61}, {"ADC", Mode::IndY, 0x71},
    {"AND", Mode::Imm, 0x29},  {"AND", Mode::Zp, 0x25},
    {"AND", Mode::ZpX, 0x35},  {"AND", Mode::Abs, 0x2D},
    {"AND", Mode::AbsX, 0x3D}, {"AND", Mode::AbsY, 0x39},
    {"AND", Mode::IndX, 0x21}, {"AND", Mode::IndY, 0x31},
    {"ASL", Mode::Acc, 0x0A},  {"ASL", Mode::Zp, 0x06},
    {"ASL", Mode::ZpX, 0x16},  {"ASL", Mode::Abs, 0x0E},
    {"ASL", Mode::AbsX, 0x1E},
    {"BCC", Mode::Rel, 0x90},  {"BCS", Mode::Rel, 0xB0},
    {"BEQ", Mode::Rel, 0xF0},  {"BMI", Mode::Rel, 0x30},
    {"BNE", Mode::Rel, 0xD0},  {"BPL", Mode::Rel, 0x10},
    {"BVC", Mode::Rel, 0x50},  {"BVS", Mode::Rel, 0x70},
    {"BIT", Mode::Zp, 0x24},   {"BIT", Mode::Abs, 0x2C},
    {"BRK", Mode::Imp, 0x00},  {"CLC", Mode::Imp, 0x18},
    {"CLD", Mode::Imp, 0xD8},  {"CLI", Mode::Imp, 0x58},
    {"CLV", Mode::Imp, 0xB8},
    {"CMP", Mode::Imm, 0xC9},  {"CMP", Mode::Zp, 0xC5},
    {"CMP", Mode::ZpX, 0xD5},  {"CMP", Mode::Abs, 0xCD},
    {"CMP", Mode::AbsX, 0xDD}, {"CMP", Mode::AbsY, 0xD9},
    {"CMP", Mode::IndX, 0xC1}, {"CMP", Mode::IndY, 0xD1},
    {"CPX", Mode::Imm, 0xE0},  {"CPX", Mode::Zp, 0xE4},
    {"CPX", Mode::Abs, 0xEC},
    {"CPY", Mode::Imm, 0xC0},  {"CPY", Mode::Zp, 0xC4},
    {"CPY", Mode::Abs, 0xCC},
    {"DEC", Mode::Zp, 0xC6},   {"DEC", Mode::ZpX, 0xD6},
    {"DEC", Mode::Abs, 0xCE},  {"DEC", Mode::AbsX, 0xDE},
    {"DEX", Mode::Imp, 0xCA},  {"DEY", Mode::Imp, 0x88},
    {"EOR", Mode::Imm, 0x49},  {"EOR", Mode::Zp, 0x45},
    {"EOR", Mode::ZpX, 0x55},  {"EOR", Mode::Abs, 0x4D},
    {"EOR", Mode::AbsX, 0x5D}, {"EOR", Mode::AbsY, 0x59},
    {"EOR", Mode::IndX, 0x41}, {"EOR", Mode::IndY, 0x51},
    {"INC", Mode::Zp, 0xE6},   {"INC", Mode::ZpX, 0xF6},
    {"INC", Mode::Abs, 0xEE},  {"INC", Mode::AbsX, 0xFE},
    {"INX", Mode::Imp, 0xE8},  {"INY", Mode::Imp, 0xC8},
    {"JMP", Mode::Abs, 0x4C},  {"JMP", Mode::Ind, 0x6C},
    {"JSR", Mode::Abs, 0x20},
    {"LDA", Mode::Imm, 0xA9},  {"LDA", Mode::Zp, 0xA5},
    {"LDA", Mode::ZpX, 0xB5},  {"LDA", Mode::Abs, 0xAD},
    {"LDA", Mode::AbsX, 0xBD}, {"LDA", Mode::AbsY, 0xB9},
    {"LDA", Mode::IndX, 0xA1}, {"LDA", Mode::IndY, 0xB1},
    {"LDX", Mode::Imm, 0xA2},  {"LDX", Mode::Zp, 0xA6},
    {"LDX", Mode::ZpY, 0xB6},  {"LDX", Mode::Abs, 0xAE},
    {"LDX", Mode::AbsY, 0xBE},
    {"LDY", Mode::Imm, 0xA0},  {"LDY", Mode::Zp, 0xA4},
    {"LDY", Mode::ZpX, 0xB4},  {"LDY", Mode::Abs, 0xAC},
    {"LDY", Mode::AbsX, 0xBC},
    {"LSR", Mode::Acc, 0x4A},  {"LSR", Mode::Zp, 0x46},
    {"LSR", Mode::ZpX, 0x56},  {"LSR", Mode::Abs, 0x4E},
    {"LSR", Mode::AbsX, 0x5E},
    {"NOP", Mode::Imp, 0xEA},
    {"ORA", Mode::Imm, 0x09},  {"ORA", Mode::Zp, 0x05},
    {"ORA", Mode::ZpX, 0x15},  {"ORA", Mode::Abs, 0x0D},
    {"ORA", Mode::AbsX, 0x1D}, {"ORA", Mode::AbsY, 0x19},
    {"ORA", Mode::IndX, 0x01}, {"ORA", Mode::IndY, 0x11},
    {"PHA", Mode::Imp, 0x48},  {"PHP", Mode::Imp, 0x08},
    {"PLA", Mode::Imp, 0x68},  {"PLP", Mode::Imp, 0x28},
    {"ROL", Mode::Acc, 0x2A},  {"ROL", Mode::Zp, 0x26},
    {"ROL", Mode::ZpX, 0x36},  {"ROL", Mode::Abs, 0x2E},
    {"ROL", Mode::AbsX, 0x3E},
    {"ROR", Mode::Acc, 0x6A},  {"ROR", Mode::Zp, 0x66},
    {"ROR", Mode::ZpX, 0x76},  {"ROR", Mode::Abs, 0x6E},
    {"ROR", Mode::AbsX, 0x7E},
    {"RTI", Mode::Imp, 0x40},  {"RTS", Mode::Imp, 0x60},
    {"SBC", Mode::Imm, 0xE9},  {"SBC", Mode::Zp, 0xE5},
    {"SBC", Mode::ZpX, 0xF5},  {"SBC", Mode::Abs, 0xED},
    {"SBC", Mode::AbsX, 0xFD}, {"SBC", Mode::AbsY, 0xF9},
    {"SBC", Mode::IndX, 0xE1}, {"SBC", Mode::IndY, 0xF1},
    {"SEC", Mode::Imp, 0x38},  {"SED", Mode::Imp, 0xF8},
    {"SEI", Mode::Imp, 0x78},
    {"STA", Mode::Zp, 0x85},   {"STA", Mode::ZpX, 0x95},
    {"STA", Mode::Abs, 0x8D},  {"STA", Mode::AbsX, 0x9D},
    {"STA", Mode::AbsY, 0x99}, {"STA", Mode::IndX, 0x81},
    {"STA", Mode::IndY, 0x91},
    {"STX", Mode::Zp, 0x86},   {"STX", Mode::ZpY, 0x96},
    {"STX", Mode::Abs, 0x8E},
    {"STY", Mode::Zp, 0x84},   {"STY", Mode::ZpX, 0x94},
    {"STY", Mode::Abs, 0x8C},
    {"TAX", Mode::Imp, 0xAA},  {"TAY", Mode::Imp, 0xA8},
    {"TSX", Mode::Imp, 0xBA},  {"TXA", Mode::Imp, 0x8A},
    {"TXS", Mode::Imp, 0x9A},  {"TYA", Mode::Imp, 0x98},
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

bool isBranch(const std::string& name) {
    return opSupports(name, Mode::Rel);
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
            } else {
                while (i < line.size() &&
                       std::isdigit(static_cast<unsigned char>(line[i])) != 0) {
                    value = value * 10 + (line[i] - '0');
                    ++i;
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
        if (std::strchr("#(),+-*/&|^<>=:.@", ch) != nullptr) {
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
            !isDirective(tokens[0].text) && !isMnemonic(tokens[0].text)) {
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
        const std::string head = tokens[pos].text;
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
        return pos + 1 < tokens.size() && tokens[pos].kind == TokKind::Punct &&
               tokens[pos].text == "," && tokens[pos + 1].kind == TokKind::Ident &&
               tokens[pos + 1].text == reg;
    }

    void parseOperand(Statement& stmt, const std::vector<Tok>& tokens,
                      std::size_t pos, const std::string& file, int line) {
        const std::string& name = stmt.mnemonic;
        if (pos >= tokens.size()) {
            // `ASL` with nothing after it shifts the accumulator: the 6502's
            // implied form does not exist for the shift mnemonics.
            stmt.mode = opSupports(name, Mode::Acc) ? Mode::Acc : Mode::Imp;
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
            stmt.mode = Mode::Imm;
            return;
        }
        if (tokens[pos].kind == TokKind::Punct && tokens[pos].text == "(") {
            // `(expr,X)` or `(expr),Y` or `(expr)`
            std::size_t close = pos + 1;
            int depth = 1;
            while (close < tokens.size() && depth > 0) {
                if (tokens[close].kind == TokKind::Punct) {
                    if (tokens[close].text == "(") {
                        ++depth;
                    } else if (tokens[close].text == ")") {
                        --depth;
                    }
                }
                ++close;
            }
            if (depth != 0) {
                throw Error(file, line, "unbalanced parentheses");
            }
            const std::size_t innerEnd = close - 1;
            std::size_t exprEnd = innerEnd;
            bool indexedX = false;
            if (innerEnd >= 2 && tokens[innerEnd - 2].kind == TokKind::Punct &&
                tokens[innerEnd - 2].text == "," &&
                tokens[innerEnd - 1].kind == TokKind::Ident &&
                tokens[innerEnd - 1].text == "X") {
                indexedX = true;
                exprEnd = innerEnd - 2;
            }
            std::vector<Tok> exprTokens(tokens.begin() + static_cast<long>(pos) + 1,
                                        tokens.begin() + static_cast<long>(exprEnd));
            ExprParser parser(exprTokens, 0, file, line);
            stmt.operand = parser.parse();
            if (indexedX) {
                stmt.mode = Mode::IndX;
                return;
            }
            if (close < tokens.size() && tokens[close].kind == TokKind::Punct &&
                tokens[close].text == ",") {
                if (close + 1 < tokens.size() &&
                    tokens[close + 1].kind == TokKind::Ident &&
                    tokens[close + 1].text == "Y") {
                    stmt.mode = Mode::IndY;
                    return;
                }
                throw Error(file, line, "expected Y after (indirect),");
            }
            stmt.mode = Mode::Ind;
            return;
        }
        // Plain expression, optionally `,X` or `,Y`.
        std::size_t exprEnd = tokens.size();
        bool indexedX = false;
        bool indexedY = false;
        if (tokens.size() >= 2 && looksLikeIndex(tokens, tokens.size() - 2, "X")) {
            indexedX = true;
            exprEnd = tokens.size() - 2;
        } else if (tokens.size() >= 2 &&
                   looksLikeIndex(tokens, tokens.size() - 2, "Y")) {
            indexedY = true;
            exprEnd = tokens.size() - 2;
        }
        std::vector<Tok> exprTokens(tokens.begin() + static_cast<long>(pos),
                                    tokens.begin() + static_cast<long>(exprEnd));
        ExprParser parser(exprTokens, 0, file, line);
        stmt.operand = parser.parse();
        if (indexedX) {
            if (opSupports(name, Mode::ZpX)) {
                stmt.mode = Mode::ZpX;  // resolved to AbsX if the value is large
            } else {
                stmt.mode = Mode::AbsX;
            }
            return;
        }
        if (indexedY) {
            if (opSupports(name, Mode::ZpY)) {
                stmt.mode = Mode::ZpY;
            } else {
                stmt.mode = Mode::AbsY;
            }
            return;
        }
        if (isBranch(name)) {
            stmt.mode = Mode::Rel;
            return;
        }
        stmt.mode = opSupports(name, Mode::Zp) ? Mode::Zp : Mode::Abs;
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
                    if (where < 0 || where > 0xFFFF) {
                        throw Error(stmt.file, stmt.line,
                                    ".org outside the 64K address space");
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
        if (address > 0xFFFF) {
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
        if (mode == Mode::ZpX && (value < 0 || value > 0xFF) &&
            opSupports(stmt.mnemonic, Mode::AbsX)) {
            mode = Mode::AbsX;
        } else if (mode == Mode::ZpY && (value < 0 || value > 0xFF) &&
                   opSupports(stmt.mnemonic, Mode::AbsY)) {
            mode = Mode::AbsY;
        } else if (mode == Mode::Zp && (value < 0 || value > 0xFF) &&
                   opSupports(stmt.mnemonic, Mode::Abs)) {
            mode = Mode::Abs;
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
        emitByte(current, stmt, emit, op->code);
        switch (mode) {
            case Mode::Imp:
            case Mode::Acc:
                return;
            case Mode::Imm:
            case Mode::Zp:
            case Mode::ZpX:
            case Mode::ZpY:
            case Mode::IndX:
            case Mode::IndY:
                emitByte(current, stmt, emit,
                         static_cast<std::uint8_t>(value & 0xFF));
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
    out << "// Generated by kestrel_asm from roms/kestrel/" << name
        << ".s -- do not edit.\n";
    out << "//\n";
    out << "// The " << name << " ROM: " << image.size()
        << " bytes of 6502 machine code,\n";
    out << "// assembled at $" << std::hex << std::uppercase << origin
        << std::dec << ". This is what the machine boots.\n\n";
    out << "#pragma once\n\n";
    out << "#include <array>\n";
    out << "#include <cstdint>\n\n";
    out << "namespace timewarp::kestrel::rom {\n\n";
    out << "inline constexpr std::uint16_t k" << upper << "Origin = 0x"
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
    out << "}  // namespace timewarp::kestrel::rom\n";
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
