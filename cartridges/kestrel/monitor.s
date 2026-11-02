; SPDX-License-Identifier: MIT
;
; monitor.s -- the Kestrel Machine Monitor, a cartridge for the 1982 machine.
;
; Four kilobytes that turned the machine into something a programmer could use:
; an address, the bytes at it, and a way to change them. The commands are one
; letter and an address in hex:
;
;   D 8000            dump sixty-four bytes from $8000
;   M 8000 42 43 44   put those bytes at $8000
;   J E000            go there
;   B                 leave the monitor and start the language again
;
; The KERNAL's reset looks at $9000 for the four letters below and, when it
; finds them, jumps to the word after them.

        .include "cart.inc"

        .org CART

CM_SIG:
        .text "KSTL"
CM_TABLE:
        .word CM_START

; ---------------------------------------------------------------------------
; Start, banner, prompt
; ---------------------------------------------------------------------------

CM_START:
        LDA #BLACK
        STA IO_PAPER
        LDA #LIGHTGREEN
        STA IO_INK
        JSR K_CLS
        LDA #<CM_BANNER
        LDX #>CM_BANNER
        JSR CM_SAY
        JSR CM_NEWLINE

CM_LOOP:
        LDA #<CM_PROMPT
        LDX #>CM_PROMPT
        JSR CM_SAY
        JSR K_GETLINE
        JSR CM_NEWLINE
        LDA ZP_INLEN
        BEQ CM_LOOP             ; an empty line asks again
        LDA INBUF
        CMP #'D'
        BEQ CM_DUMP
        CMP #'M'
        BEQ CM_MODIFY
        CMP #'J'
        BEQ CM_JUMP
        CMP #'B'
        BEQ CM_BASIC
        LDA #'?'
        JSR K_CHROUT
        JMP CM_LOOP

CM_BASIC:
        JMP COLD_START

CM_JUMP:
        JSR CM_PARSE
        BCS CM_ERROR
        JMP (V_PTR)

; ---------------------------------------------------------------------------
; D: sixty-four bytes, eight to a row, each row led by its address
; ---------------------------------------------------------------------------

CM_DUMP:
        JSR CM_PARSE
        BCS CM_ERROR
        LDA #8
        STA V_COUNT             ; rows still to print
CM_DUMP_ROW:
        LDA V_PTR+1
        JSR CM_PUTHEX
        LDA V_PTR
        JSR CM_PUTHEX
        LDA #':'
        JSR K_CHROUT
        LDA #' '
        JSR K_CHROUT
        LDX #8
CM_DUMP_BYTE:
        STX V_INDEX
        LDA V_PTR               ; the KERNAL has used the zero page, so the
        STA ZP_PTR              ; pointer is put back for the one read that
        LDA V_PTR+1             ; needs it
        STA ZP_PTR+1
        LDY #0
        LDA (ZP_PTR),Y
        JSR CM_PUTHEX
        LDA #' '
        JSR K_CHROUT
        INC V_PTR
        BNE CM_DUMP_NEXT
        INC V_PTR+1
CM_DUMP_NEXT:
        LDX V_INDEX
        DEX
        BNE CM_DUMP_BYTE
        JSR CM_NEWLINE
        DEC V_COUNT
        BNE CM_DUMP_ROW
        JMP CM_LOOP

; ---------------------------------------------------------------------------
; M: bytes after the address, two hex digits each, written as they are read
; ---------------------------------------------------------------------------

CM_MODIFY:
        JSR CM_PARSE
        BCS CM_ERROR
        STY V_INDEX             ; where the digits begin in the line
        LDA #0
        STA V_COUNT             ; how many bytes have gone in
CM_MOD_LOOP:
        LDY V_INDEX
        CPY ZP_INLEN
        BCS CM_MOD_DONE
        LDA INBUF,Y
        CMP #' '
        BNE CM_MOD_BYTE
        INY
        STY V_INDEX
        JMP CM_MOD_LOOP
CM_MOD_BYTE:
        JSR CM_HEXVALUE
        BCS CM_ERROR
        ASL A
        ASL A
        ASL A
        ASL A
        STA V_TEMP              ; the high digit, waiting
        INY
        CPY ZP_INLEN
        BCS CM_ERROR            ; a digit on its own is not a byte
        LDA INBUF,Y
        JSR CM_HEXVALUE
        BCS CM_ERROR
        ORA V_TEMP
        STA V_TEMP
        INY                     ; past the low digit, before Y is spent
        STY V_INDEX
        LDA V_PTR
        STA ZP_PTR
        LDA V_PTR+1
        STA ZP_PTR+1
        LDY #0
        LDA V_TEMP
        STA (ZP_PTR),Y
        INC V_PTR
        BNE CM_MOD_COUNT
        INC V_PTR+1
CM_MOD_COUNT:
        INC V_COUNT
        JMP CM_MOD_LOOP
CM_MOD_DONE:
        LDA V_COUNT
        JSR CM_PUTHEX
        LDA #' '
        JSR K_CHROUT
        LDX #0
CM_MOD_SAY:
        LDA CM_BYTES,X
        BEQ CM_MOD_SAID
        JSR K_CHROUT
        INX
        JMP CM_MOD_SAY
CM_MOD_SAID:
        JSR CM_NEWLINE
        JMP CM_LOOP

CM_ERROR:
        LDA #'?'
        JSR K_CHROUT
        JSR CM_NEWLINE
        JMP CM_LOOP

; ---------------------------------------------------------------------------
; The pieces: a string, a newline, a parse, and hex in both directions
; ---------------------------------------------------------------------------

; CM_SAY: print the zero-terminated string whose address is in A and X.
CM_SAY:
        STA ZP_STRPTR
        STX ZP_STRPTR+1
        JMP K_PRINT

CM_NEWLINE:
        LDA #CC_RETURN
        JMP K_CHROUT

; CM_PARSE: the four hex digits after the command letter -> V_PTR, and Y is the
; first character after them. Carry set when they are not four hex digits.
CM_PARSE:
        LDY #1
CM_PARSE_SPACE:
        LDA INBUF,Y
        CMP #' '
        BNE CM_PARSE_START
        INY
        JMP CM_PARSE_SPACE
CM_PARSE_START:
        LDA #0
        STA V_PTR
        STA V_PTR+1
        LDX #4
CM_PARSE_LOOP:
        LDA INBUF,Y
        JSR CM_HEXVALUE
        BCS CM_PARSE_BAD
        STA V_TEMP
        ASL V_PTR
        ROL V_PTR+1
        ASL V_PTR
        ROL V_PTR+1
        ASL V_PTR
        ROL V_PTR+1
        ASL V_PTR
        ROL V_PTR+1
        LDA V_PTR
        ORA V_TEMP
        STA V_PTR
        INY
        DEX
        BNE CM_PARSE_LOOP
        CLC
        RTS
CM_PARSE_BAD:
        SEC
        RTS

; CM_HEXVALUE: A = a character -> A = 0 to 15, carry set when it is not one.
CM_HEXVALUE:
        CMP #'0'
        BCC CM_HEX_BAD
        CMP #'9'+1
        BCS CM_HEX_LETTER
        SEC
        SBC #'0'
        CLC
        RTS
CM_HEX_LETTER:
        AND #$DF                ; the letters are taken in either case
        CMP #'A'
        BCC CM_HEX_BAD
        CMP #'G'
        BCS CM_HEX_BAD
        SEC
        SBC #'A'-10
        CLC
        RTS
CM_HEX_BAD:
        SEC
        RTS

; CM_PUTHEX: A = a byte, printed as two digits.
CM_PUTHEX:
        STA V_TEMP
        LSR A
        LSR A
        LSR A
        LSR A
        JSR CM_PUTNIB
        LDA V_TEMP
        AND #$0F
        JMP CM_PUTNIB

CM_PUTNIB:
        CMP #10
        BCC CM_PUTDIGIT
        CLC
        ADC #'A'-10
        JMP K_CHROUT
CM_PUTDIGIT:
        CLC
        ADC #'0'
        JMP K_CHROUT

CM_BANNER:
        .text "KESTREL MACHINE MONITOR"
        .byte 0
CM_PROMPT:
        .text ">"
        .byte 0
CM_BYTES:
        .text "BYTES"
        .byte 0
