; SPDX-License-Identifier: MIT
;
; tutor.s -- Kestrel Tutor, a cartridge for the 1982 machine.
;
; One of the ways a home computer was sold in 1982 was as a teacher. This one
; sets the multiplication tables, marks the answer and keeps a score: type the
; answer and press Return.
;
; The questions come from the machine's noise register, so they are not the same
; every evening.

        .include "cart.inc"

        .org CART

TU_SIG:
        .text "KSTL"
TU_TABLE:
        .word TU_START

TU_START:
        LDA #BLACK
        STA IO_PAPER
        LDA #LIGHTGREEN
        STA IO_INK
        JSR K_CLS
        LDA #<TU_BANNER
        LDX #>TU_BANNER
        JSR TU_SAY
        JSR TU_NEWLINE
        LDA #0
        STA V_SCORE
        STA V_TRIES

TU_ROUND:
        LDA IO_RANDOM           ; the noise register, nought to 255
        AND #7
        CLC
        ADC #2
        STA V_A                 ; a factor from two to nine
        LDA IO_RANDOM
        AND #3
        CLC
        ADC #2
        STA V_B                 ; and one from two to five

        LDA V_A
        JSR TU_PUTDEC
        LDA #' '
        JSR K_CHROUT
        LDA #'X'
        JSR K_CHROUT
        LDA #' '
        JSR K_CHROUT
        LDA V_B
        JSR TU_PUTDEC
        LDA #' '
        JSR K_CHROUT
        LDA #'='
        JSR K_CHROUT
        LDA #' '
        JSR K_CHROUT

        JSR TU_PRODUCT          ; V_ANSWER = A * B
        JSR K_GETLINE
        JSR TU_NEWLINE
        JSR TU_PARSE            ; V_GIVEN = the number that went in

        INC V_TRIES
        LDA V_GIVEN
        CMP V_ANSWER
        BNE TU_WRONG
        INC V_SCORE
        LDA #<TU_RIGHT
        LDX #>TU_RIGHT
        JSR TU_SAY
        JSR TU_NEWLINE
        JMP TU_SCORE
TU_WRONG:
        LDA #<TU_SORRY
        LDX #>TU_SORRY
        JSR TU_SAY
        LDA V_ANSWER
        JSR TU_PUTDEC
        JSR TU_NEWLINE
TU_SCORE:
        LDA #<TU_SCORETEXT
        LDX #>TU_SCORETEXT
        JSR TU_SAY
        LDA V_SCORE
        JSR TU_PUTDEC
        LDA #'/'
        JSR K_CHROUT
        LDA V_TRIES
        JSR TU_PUTDEC
        JSR TU_NEWLINE
        JMP TU_ROUND

; ---------------------------------------------------------------------------
; The pieces
; ---------------------------------------------------------------------------

; TU_SAY: print the zero-terminated string whose address is in A and X.
TU_SAY:
        STA ZP_STRPTR
        STX ZP_STRPTR+1
        JMP K_PRINT

TU_NEWLINE:
        LDA #CC_RETURN
        JMP K_CHROUT

; TU_PRODUCT: V_ANSWER = V_A * V_B, by addition: the processor has no multiply
; and the factors are small.
TU_PRODUCT:
        LDA #0
        STA V_ANSWER
        LDX V_B
TU_PRODUCT_LOOP:
        LDA V_ANSWER
        CLC
        ADC V_A
        STA V_ANSWER
        DEX
        BNE TU_PRODUCT_LOOP
        RTS

; TU_PARSE: the digits at the start of INBUF -> V_GIVEN. Anything else ends the
; reading, so an empty line answers nought.
TU_PARSE:
        LDA #0
        STA V_GIVEN
        LDY #0
TU_PARSE_LOOP:
        CPY ZP_INLEN
        BCS TU_PARSE_DONE
        LDA INBUF,Y
        CMP #'0'
        BCC TU_PARSE_DONE
        CMP #'9'+1
        BCS TU_PARSE_DONE
        SEC
        SBC #'0'
        STA V_TEMP              ; the digit
        LDA V_GIVEN
        ASL A                   ; times ten is times two and times eight
        STA V_GIVEN
        ASL A
        ASL A
        CLC
        ADC V_GIVEN
        CLC
        ADC V_TEMP
        STA V_GIVEN
        INY
        JMP TU_PARSE_LOOP
TU_PARSE_DONE:
        RTS

; TU_PUTDEC: A = a byte, printed in decimal without leading zeros.
TU_PUTDEC:
        STA V_TEMP
        LDX #0                  ; the hundreds
TU_PUTDEC_HUNDREDS:
        LDA V_TEMP
        CMP #100
        BCC TU_PUTDEC_TENS
        SEC
        SBC #100
        STA V_TEMP
        INX
        JMP TU_PUTDEC_HUNDREDS
TU_PUTDEC_TENS:
        STX V_FLAG              ; anything above the tens digit?
        CPX #0
        BEQ TU_PUTDEC_TENS_GO
        TXA
        JSR TU_DIGIT
TU_PUTDEC_TENS_GO:
        LDX #0
TU_PUTDEC_TENS_LOOP:
        LDA V_TEMP
        CMP #10
        BCC TU_PUTDEC_UNITS
        SEC
        SBC #10
        STA V_TEMP
        INX
        JMP TU_PUTDEC_TENS_LOOP
TU_PUTDEC_UNITS:
        CPX #0
        BNE TU_PUTDEC_TENS_OUT
        LDA V_FLAG
        BEQ TU_PUTDEC_LAST
TU_PUTDEC_TENS_OUT:
        TXA
        JSR TU_DIGIT
TU_PUTDEC_LAST:
        LDA V_TEMP
        JMP TU_DIGIT

TU_DIGIT:
        CLC
        ADC #'0'
        JMP K_CHROUT

TU_BANNER:
        .text "KESTREL TUTOR"
        .byte 0
TU_RIGHT:
        .text "RIGHT"
        .byte 0
TU_SORRY:
        .text "NO, IT IS "
        .byte 0
TU_SCORETEXT:
        .text "SCORE "
        .byte 0
