// SPDX-License-Identifier: MIT
//
// The 65816 assembler.
//
// Every encoding here is written out by hand from the processor's own opcode
// map rather than recorded from the tool, so a bug in the table cannot pass by
// agreeing with itself. The tool is run as a program, which is how the build
// uses it.

#include "check.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::uint32_t kOrigin = 0x8000;
constexpr std::uint32_t kSize = 0x1000;

struct Case {
    const char* source;
    std::vector<std::uint8_t> expected;
    const char* what;
};

const std::vector<Case>& cases() {
    static const std::vector<Case> all{
        // Immediates. Without a suffix an immediate is sixteen bits, because
        // that is what native mode runs; `.B` and `.W` say otherwise.
        {"LDA #$12", {0xA9, 0x12}, "immediate, a byte by default"},
        {"LDA.W #$1234", {0xA9, 0x34, 0x12}, "immediate, forced word"},
        {"LDA.B #$12", {0xA9, 0x12}, "immediate, byte"},
        {"LDX.B #$12", {0xA2, 0x12}, "index register immediate, byte"},
        {"LDY.W #$1234", {0xA0, 0x34, 0x12}, "index register immediate, word"},
        {"ADC.B #$01", {0x69, 0x01}, "add with carry, byte"},
        {"REP #$30", {0xC2, 0x30}, "reset status bits"},
        {"SEP #$20", {0xE2, 0x20}, "set status bits"},
        {"PEA $1234", {0xF4, 0x34, 0x12}, "push effective absolute"},
        {"COP #$02", {0x02, 0x02}, "coprocessor signature"},

        // Direct page and its indirections.
        {"LDA $10", {0xA5, 0x10}, "direct page"},
        {"LDA $10,X", {0xB5, 0x10}, "direct page, X"},
        {"LDX $10,Y", {0xB6, 0x10}, "direct page, Y"},
        {"LDA ($10)", {0xB2, 0x10}, "direct page indirect"},
        {"LDA ($10,X)", {0xA1, 0x10}, "direct page indexed indirect"},
        {"LDA ($10),Y", {0xB1, 0x10}, "direct page indirect indexed"},
        {"STA ($10),Y", {0x91, 0x10}, "store through (dp),Y"},
        {"LDA [$10]", {0xA7, 0x10}, "direct page indirect long"},
        {"LDA [$10],Y", {0xB7, 0x10}, "direct page indirect long indexed"},
        {"STA [$10]", {0x87, 0x10}, "store through [dp]"},
        {"LDA $10,S", {0xA3, 0x10}, "stack relative"},
        {"LDA ($10,S),Y", {0xB3, 0x10}, "stack relative indirect indexed"},

        // Absolute, absolute long, and the jump forms that differ.
        {"LDA $1234", {0xAD, 0x34, 0x12}, "absolute"},
        {"LDA $1234,X", {0xBD, 0x34, 0x12}, "absolute, X"},
        {"LDX $1234,Y", {0xBE, 0x34, 0x12}, "absolute, Y"},
        {"JMP ($1234)", {0x6C, 0x34, 0x12}, "jump indirect"},
        {"JMP ($1234,X)", {0x7C, 0x34, 0x12}, "jump indexed indirect"},
        {"JSR ($1234,X)", {0xFC, 0x34, 0x12}, "jump to subroutine, indexed indirect"},
        {"JMP $1234", {0x4C, 0x34, 0x12}, "jump absolute"},
        {"JSR $1234", {0x20, 0x34, 0x12}, "jump to subroutine"},
        {"JML [$1234]", {0xDC, 0x34, 0x12}, "jump indirect long"},
        {"LDA $7E:$2100", {0xAF, 0x00, 0x21, 0x7E}, "bank-qualified operand"},
        {"STA $7E:$2100", {0x8F, 0x00, 0x21, 0x7E}, "bank-qualified store"},
        {"LDA.L $1234", {0xAF, 0x34, 0x12, 0x00}, "long forced on a short value"},
        {"LDA $123456,X", {0xBF, 0x56, 0x34, 0x12}, "wide operand picks long, X"},
        {"JSL $C01234", {0x22, 0x34, 0x12, 0xC0}, "jump to subroutine long"},
        {"JML $C01234", {0x5C, 0x34, 0x12, 0xC0}, "jump long"},

        // Implied and stack instructions.
        {"PHB", {0x8B}, "push data bank"},
        {"PLB", {0xAB}, "pull data bank"},
        {"PHD", {0x0B}, "push direct page"},
        {"PHK", {0x4B}, "push program bank"},
        {"TCD", {0x5B}, "transfer accumulator to direct page"},
        {"XCE", {0xFB}, "exchange carry and emulation"},
        {"RTL", {0x6B}, "return long"},
        {"WAI", {0xCB}, "wait for interrupt"},
        {"PHX", {0xDA}, "push X"},
        {"ASL", {0x0A}, "shift the accumulator when nothing follows"},

        // Block moves take two banks, destination first.
        {"MVN $7E,$7F", {0x54, 0x7E, 0x7F}, "block move"},
        {"MVP $01,$02", {0x44, 0x01, 0x02}, "block move, previous"},
    };
    return all;
}

bool assemble(const fs::path& tool, const std::string& source,
              std::vector<std::uint8_t>& out) {
    const fs::path dir = fs::temp_directory_path() / "kestrel16_asm_tests";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path src = dir / "case.s";
    const fs::path bin = dir / "case.bin";
    {
        std::ofstream file{src};
        // A sentinel after the code makes every case length-exact: the window is
        // padded to its full size, so the tail would otherwise go unchecked.
        file << "  .org $" << std::hex << kOrigin << std::dec << "\n" << source
             << "\n  .byte $EE\n";
    }
    fs::remove(bin, ec);
    const std::string command = "\"" + tool.string() + "\" \"" + src.string() +
                                "\" --origin " + std::to_string(kOrigin) +
                                " --size " + std::to_string(kSize) + " --bin \"" +
                                bin.string() + "\" > /dev/null 2>&1";
    if (std::system(command.c_str()) != 0) {
        std::printf("     assembly failed for:\n%s\n", source.c_str());
        return false;
    }
    std::ifstream in{bin, std::ios::binary};
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>{in},
               std::istreambuf_iterator<char>{});
    return true;
}

std::string bytesOf(const std::vector<std::uint8_t>& bytes) {
    std::string text;
    char buffer[8];
    for (const std::uint8_t byte : bytes) {
        std::snprintf(buffer, sizeof(buffer), "%02X ", byte);
        text += buffer;
    }
    if (!text.empty()) {
        text.pop_back();
    }
    return text;
}

/// The code plus its sentinel, so a case that emits too few bytes fails too.
std::vector<std::uint8_t> withSentinel(std::vector<std::uint8_t> bytes) {
    bytes.push_back(0xEE);
    return bytes;
}

std::vector<std::uint8_t> leading(const std::vector<std::uint8_t>& bytes,
                                 std::size_t count) {
    if (bytes.size() < count) {
        return bytes;
    }
    return {bytes.begin(), bytes.begin() + static_cast<long>(count)};
}

void expect(const fs::path& tool, const char* source,
            const std::vector<std::uint8_t>& expected) {
    std::vector<std::uint8_t> got;
    if (!assemble(tool, source, got)) {
        TW_CHECK(false);
        return;
    }
    const auto wanted = withSentinel(expected);
    const auto seen = leading(got, wanted.size());
    TW_CHECK_EQ(bytesOf(seen), bytesOf(wanted));
    if (seen != wanted) {
        std::printf("     in: %s\n", source);
    }
}

void test_encodings(const fs::path& tool) {
    for (const Case& test : cases()) {
        expect(tool, test.source, test.expected);
    }
}

void test_labels_and_branches(const fs::path& tool) {
    // A branch back to its own line is four bytes from the opcode.
    expect(tool, "again:\n  LDA $10\n  BNE again", {0xA5, 0x10, 0xD0, 0xFC});
    // The long branch reaches sixteen bits without help.
    expect(tool, "again:\n  LDA $10\n  BRL again",
           {0xA5, 0x10, 0x82, 0xFB, 0xFF});
    // A near branch that cannot reach becomes the inverse branch over a JMP,
    // which is how a 6502 assembler has to express it.
    {
        std::vector<std::uint8_t> expected{0xF0, 0x03, 0x4C, 0xCD, 0x80};
        expected.insert(expected.end(), 200, 0xEA);
        expected.push_back(0x60);
        expect(tool, "  BNE far\n  .fill 200, $EA\nfar:\n  RTS", expected);
    }
    // A forward reference that has to settle on the wide form still converges.
    expect(tool, "  .word later\n  NOP\nlater:\n  RTS",
           {0x03, 0x80, 0xEA, 0x60});
}

void test_dialect(const fs::path& tool) {
    // Data directives carry bytes through untouched.
    expect(tool, "  .byte $41, $42\n  .text \"HI\"\n  .byte 0",
           {0x41, 0x42, 0x48, 0x49, 0x00});
    // A mnemonic with a width suffix is still recognised in the label field, so
    // `LDA.B` at the start of a line is not mistaken for a label.
    expect(tool, "  LDA.B #$01\n  LDA.W #$0203", {0xA9, 0x01, 0xA9, 0x03, 0x02});
}

}  // namespace

int main() {
    const fs::path tool{KESTREL16_ASM_PATH};
    test_encodings(tool);
    test_labels_and_branches(tool);
    test_dialect(tool);
    return tw::test::summary("kestrel16_asm_tests");
}
