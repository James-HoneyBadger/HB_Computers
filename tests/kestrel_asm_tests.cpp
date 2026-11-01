// SPDX-License-Identifier: MIT
//
// The 6502 assembler: the dialect the Kestrel's ROMs are written in.
//
// Every encoding here is written out by hand from the processor's own opcode
// map rather than recorded from the tool, so a bug in the table cannot pass by
// agreeing with itself. The tool is run as a program, which is how the build
// uses it.

#include "check.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::uint32_t kOrigin = 0x8000;
constexpr std::uint32_t kSize = 0x1000;

/// A sentinel after the code makes every case length-exact: the image is padded
/// to its full size, so the tail would otherwise go unchecked.
constexpr std::uint8_t kSentinel = 0xEE;

std::string commandFor(const fs::path& tool, const fs::path& src, const fs::path& bin) {
    return "\"" + tool.string() + "\" \"" + src.string() + "\" --origin " +
           std::to_string(kOrigin) + " --size " + std::to_string(kSize) +
           " --bin \"" + bin.string() + "\" > /dev/null 2>&1";
}

bool assemble(const fs::path& tool, const std::string& source,
              std::vector<std::uint8_t>& out) {
    const fs::path dir = fs::temp_directory_path() / "kestrel_asm_tests";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path src = dir / "case.s";
    const fs::path bin = dir / "case.bin";
    {
        std::ofstream file{src};
        file << "  .org $" << std::hex << std::uppercase << kOrigin << std::dec
             << "\n" << source << "\n  .byte $" << std::hex << std::uppercase
             << static_cast<int>(kSentinel) << std::dec << "\n";
    }
    fs::remove(bin, ec);
    if (std::system(commandFor(tool, src, bin).c_str()) != 0) {
        std::printf("     assembly failed for:\n%s\n", source.c_str());
        return false;
    }
    std::ifstream in{bin, std::ios::binary};
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
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

void expect(const fs::path& tool, const char* source,
            const std::vector<std::uint8_t>& expected) {
    std::vector<std::uint8_t> got;
    if (!assemble(tool, source, got)) {
        TW_CHECK(false);
        return;
    }
    std::vector<std::uint8_t> wanted = expected;
    wanted.push_back(kSentinel);
    const std::vector<std::uint8_t> seen =
        got.size() < wanted.size()
            ? got
            : std::vector<std::uint8_t>{got.begin(),
                                        got.begin() + static_cast<long>(wanted.size())};
    TW_CHECK_EQ(bytesOf(seen), bytesOf(wanted));
    if (seen != wanted) {
        std::printf("     in: %s\n", source);
    }
}

/// A source the assembler has to refuse.
void expectRefused(const fs::path& tool, const char* source, const char* because) {
    const fs::path dir = fs::temp_directory_path() / "kestrel_asm_tests";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path src = dir / "bad.s";
    const fs::path bin = dir / "bad.bin";
    {
        std::ofstream file{src};
        file << "  .org $" << std::hex << std::uppercase << kOrigin << std::dec
             << "\n" << source << "\n";
    }
    fs::remove(bin, ec);
    const bool refused = std::system(commandFor(tool, src, bin).c_str()) != 0;
    TW_CHECK(refused);
    if (!refused) {
        std::printf("     accepted: %s (%s)\n", source, because);
    }
}

void test_every_addressing_mode(const fs::path& tool) {
    expect(tool, "  LDA #$12", {0xA9, 0x12});
    expect(tool, "  LDA $10", {0xA5, 0x10});
    expect(tool, "  LDA $10,X", {0xB5, 0x10});
    expect(tool, "  LDX $10,Y", {0xB6, 0x10});
    expect(tool, "  LDA $1234", {0xAD, 0x34, 0x12});
    expect(tool, "  LDA $1234,X", {0xBD, 0x34, 0x12});
    expect(tool, "  LDA $1234,Y", {0xB9, 0x34, 0x12});
    expect(tool, "  LDA ($10,X)", {0xA1, 0x10});
    expect(tool, "  LDA ($10),Y", {0xB1, 0x10});
    expect(tool, "  JMP ($1234)", {0x6C, 0x34, 0x12});
    expect(tool, "  JSR $1234", {0x20, 0x34, 0x12});
    expect(tool, "  RTS", {0x60});
    expect(tool, "  STA $8000", {0x8D, 0x00, 0x80});
    expect(tool, "  BRK", {0x00});
}

void test_numbers_and_expressions(const fs::path& tool) {
    // The two ways of writing hex, and binary, end up the same byte.
    expect(tool, "  .byte $34, %110100, 52", {0x34, 0x34, 0x34});
    expect(tool, "  .byte 0x34", {0x34});
    // Character literals, and the operators the dialect allows.
    expect(tool, "  .byte 'A', 'A'+1, <$1234, >$1234", {0x41, 0x42, 0x34, 0x12});
    expect(tool, "  .byte 1+2*3, 7&3, 8|1, 1<<4, 20>>2", {0x07, 0x03, 0x09, 0x10, 0x05});
    expect(tool, "  .word $1234+1", {0x35, 0x12});
}

void test_data_directives(const fs::path& tool) {
    expect(tool, "  .byte $41, $42\n  .text \"HI\"\n  .asciiz \"YO\"",
           {0x41, 0x42, 0x48, 0x49, 0x59, 0x4F, 0x00});
    expect(tool, "  .fill 3, $AA\n  .res 2\n  .byte $BB",
           {0xAA, 0xAA, 0xAA, 0x00, 0x00, 0xBB});
    expect(tool, "  .byte 1\n  .align 8\n  .byte 2",
           {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02});
    expect(tool, "  .dw $1234\n  .db $56", {0x34, 0x12, 0x56});
}

void test_labels_and_branches(const fs::path& tool) {
    // A branch to its own line is four bytes from the opcode: the offset counts
    // from the instruction after the branch.
    expect(tool, "again:\n  LDA $10\n  BNE again", {0xA5, 0x10, 0xD0, 0xFC});
    // Labels are case-blind.
    expect(tool, "Here: NOP\n  JMP here", {0xEA, 0x4C, 0x00, 0x80});
    // A near branch that cannot reach becomes the inverse branch over a JMP.
    {
        std::vector<std::uint8_t> far{0xF0, 0x03, 0x4C, 0xCD, 0x80};
        far.insert(far.end(), 200, 0xEA);
        far.push_back(0x60);
        expect(tool, "  BNE far\n  .fill 200, $EA\nfar:\n  RTS", far);
    }
    // A constant, and a label used before it is defined.
    expect(tool, "SIZE = 3\n  .byte SIZE\n  .word base\nbase:",
           {0x03, 0x03, 0x80});
}

void test_refusals(const fs::path& tool) {
    // A number that runs into a letter would otherwise become a silent zero.
    expectRefused(tool, "  .byte 12AB", "a number that runs into a letter");
    expectRefused(tool, "  .byte $", "empty hexadecimal number");
}

}  // namespace

int main() {
    const fs::path tool{KESTREL_ASM_PATH};
    test_every_addressing_mode(tool);
    test_numbers_and_expressions(tool);
    test_data_directives(tool);
    test_labels_and_branches(tool);
    test_refusals(tool);
    return tw::test::summary("kestrel_asm_tests");
}
