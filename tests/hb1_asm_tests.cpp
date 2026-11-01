// SPDX-License-Identifier: MIT
//
// The assembler: the images the machine boots are built from these rules, so a
// mistake here is a mistake in every ROM.

#include "check.hpp"

int main() {
    // The assembler is exercised end to end by `hb1_machine_tests`, which boots
    // what it produced. This suite is the place for the rules that are about the
    // dialect itself.
    return tw::test::summary("hb1_asm_tests");
}
