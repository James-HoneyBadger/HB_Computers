; SPDX-License-Identifier: MIT
;
; colours.s -- the Kestrel 16 Colour Demonstration, a cartridge for 1992.
;
; The machine's party piece was its tube: fifty rows of eighty, and a byte of
; colour for every cell of it. This cartridge fills the screen with colour so
; that a shop could leave the machine running in the window, which is what the
; demonstration cartridges of the day were for.
;
; It writes the tube directly and prints nothing, so what it draws is the whole
; of the screen it leaves behind.

        .include "cart.inc"

        .org CART

CV_SIG:
        .text "KSTL"
CV_TABLE:
        .word CV_START

CV_START:
        SEP #$20                ; characters are bytes
        REP #$10                ; the cell index is a word
        LDA #BLACK
        STA IO_PAPER
        STA IO_BORDER
        LDA #LIGHTGREY
        STA IO_INK

        ; A band of colour a few cells wide, the whole screen high, the hue
        ; running along the row and the brightness changing down it.
        LDX #0                  ; the cell
CV_FILL:
        TXA                     ; a texture, so the pattern is a picture and not
        LSR A                   ; only a colour: four glyphs, cycled
        LSR A
        AND #$03
        TAY
        LDA CV_GLYPHS,Y
        STA SCREEN,X
        TXA
        LSR A
        LSR A
        STA V_TEMP
        LDA V_TEMP
        LSR A
        LSR A
        LSR A
        LSR A                   ; which row of sixteen the cell is in
        EOR V_TEMP              ; and the hue of the band it belongs to
        AND #$0F
        ORA #$10                ; the same colour behind as in front
        STA ATTRS,X
        INX
        CPX #CELLS
        BNE CV_FILL

        ; The name, in the top left corner, over the pattern.
        LDX #0
CV_TITLE:
        LDA CV_TEXT,X
        BEQ CV_TITLE_DONE
        STA SCREEN,X
        LDA #(WHITE<<4)|BLACK
        STA ATTRS,X
        INX
        BRA CV_TITLE
CV_TITLE_DONE:
CV_HOLD:
        BRA CV_HOLD             ; a demonstration never ends

CV_GLYPHS:
        .text " .:*"
CV_TEXT:
        .text "KESTREL 16"
        .byte 0
