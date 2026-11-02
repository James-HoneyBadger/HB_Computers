; SPDX-License-Identifier: MIT
;
; cart.inc -- what a cartridge for the 1992 machine needs to know.
;
; A cartridge is a 128 KiB image. Its first 8 KiB answers in bank zero at $C000,
; which is where the KERNAL's reset looks for the four letters below and the word
; that follows them; the whole image also answers across banks four to seven,
; which is the cartridge's own RAM. A cartridge that fits in 8 KiB needs nothing
; but bank zero.

.include "../../roms/kestrel16/kestrel16.inc"

CART_ID      = CART+0       ; 'K','S','T','L'
CART_ENTRY   = CART+4       ; the address the reset jumps to

; What the cartridge is doing lives in RAM, not in the direct page: the KERNAL
; uses the direct page on every call, and its own bytes are not the language's to
; keep. $0800 is where a program would begin, which this cartridge replaces.
CART_VARS   = $000800
V_PTR       = CART_VARS+0   ; three bytes: an address
V_COLOUR    = CART_VARS+3
V_COUNT     = CART_VARS+4
V_TEMP      = CART_VARS+5
