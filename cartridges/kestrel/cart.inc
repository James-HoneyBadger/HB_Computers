; SPDX-License-Identifier: MIT
;
; cart.inc -- what a cartridge for the 1982 machine needs to know.
;
; A cartridge is 4 KiB at $9000 and is looked at once, by the KERNAL's reset: if
; the first four bytes are KSTL it jumps to the word that follows them. The rest
; of the slot is the program.

.include "../../roms/kestrel/kestrel.inc"

CART_ID      = CART+0       ; 'K','S','T','L'
CART_ENTRY   = CART+4       ; the address the reset jumps to

; A cartridge keeps what it is doing in RAM and not in the zero page: every call
; to the KERNAL is allowed to use every cell it likes, CHROUT included, which
; loads the screen pointer into ZP_PTR on each character. The block at $0800 is
; the program area of a language this cartridge replaces, so it is free until the
; cartridge hands the machine back with a cold start.
CART_VARS   = $0800
V_PTR       = CART_VARS+0   ; two bytes: an address being looked at
V_TEMP      = CART_VARS+2
V_COUNT     = CART_VARS+3
V_INDEX     = CART_VARS+4
V_FLAG      = CART_VARS+5
V_A         = CART_VARS+6
V_B         = CART_VARS+7
V_ANSWER    = CART_VARS+8
V_GIVEN     = CART_VARS+9
V_SCORE     = CART_VARS+10
V_TRIES     = CART_VARS+11
