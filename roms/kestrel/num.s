; SPDX-License-Identifier: MIT
;
; num.s -- numbers as a person writes them.
;
; The machine's numbers are binary fractions; the tube shows decimal. This file
; is the crossing: FPRINT turns the accumulator into the characters PRINT puts
; on the screen, and FVAL turns characters back into the accumulator for VAL and
; for INPUT.

; ---------------------------------------------------------------------------
; Constants, byte-adjacent so COPYCON can index them
; ---------------------------------------------------------------------------
CONTAB:
        .byte 129, $00, $00, $00, $00    ; 1
        .byte 132, $20, $00, $00, $00    ; 10
        .byte 128, $00, $00, $00, $00    ; 0.5
        .byte 134, $48, $00, $00, $00    ; 100

CON_ONE_I     = 0
CON_TEN_I     = 5
CON_HALF_I    = 10
CON_HUNDRED_I = 15

; COPYCON: the five-byte constant at index X becomes ARG.
COPYCON:
        LDA CONTAB,X
        STA B_ARG
        LDA CONTAB+1,X
        STA B_ARG+1
        LDA CONTAB+2,X
        STA B_ARG+2
        LDA CONTAB+3,X
        STA B_ARG+3
        LDA CONTAB+4,X
        STA B_ARG+4
        RTS

FMUL10:
        LDX #CON_TEN_I
        JSR COPYCON
        JMP FMUL

FDIV10:
        LDX #CON_TEN_I
        JSR COPYCON
        JMP FDIV

; FADDDIG: the accumulator gains the digit in A (0..9).
FADDDIG:
        STA B_TEMP
        LDA B_FAC
        STA B_FPBUF
        LDA B_FAC+1
        STA B_FPBUF+1
        LDA B_FAC+2
        STA B_FPBUF+2
        LDA B_FAC+3
        STA B_FPBUF+3
        LDA B_FAC+4
        STA B_FPBUF+4
        LDA B_TEMP
        LDY #0
        JSR ITOF
        LDA B_FAC
        STA B_ARG
        LDA B_FAC+1
        STA B_ARG+1
        LDA B_FAC+2
        STA B_ARG+2
        LDA B_FAC+3
        STA B_ARG+3
        LDA B_FAC+4
        STA B_ARG+4
        LDA B_FPBUF
        STA B_FAC
        LDA B_FPBUF+1
        STA B_FAC+1
        LDA B_FPBUF+2
        STA B_FAC+2
        LDA B_FPBUF+3
        STA B_FAC+3
        LDA B_FPBUF+4
        STA B_FAC+4
        JMP FADD

; FSUBDIG: the accumulator loses the digit in A (0..9). ITOF builds the digit
; in the accumulator, so the running value is kept aside across it.
FSUBDIG:
        STA B_FTMP
        LDA B_FAC
        STA B_FPBUF
        LDA B_FAC+1
        STA B_FPBUF+1
        LDA B_FAC+2
        STA B_FPBUF+2
        LDA B_FAC+3
        STA B_FPBUF+3
        LDA B_FAC+4
        STA B_FPBUF+4
        LDA B_FTMP
        LDY #0
        JSR ITOF
        LDA B_FAC
        STA B_ARG
        LDA B_FAC+1
        STA B_ARG+1
        LDA B_FAC+2
        STA B_ARG+2
        LDA B_FAC+3
        STA B_ARG+3
        LDA B_FAC+4
        STA B_ARG+4
        LDA B_FPBUF
        STA B_FAC
        LDA B_FPBUF+1
        STA B_FAC+1
        LDA B_FPBUF+2
        STA B_FAC+2
        LDA B_FPBUF+3
        STA B_FAC+3
        LDA B_FPBUF+4
        STA B_FAC+4
        JMP FSUB

; ---------------------------------------------------------------------------
; FPRINT: the accumulator becomes characters in B_NUMBUF, zero terminated.
; ---------------------------------------------------------------------------

FPRINT:
        LDA #0
        STA B_TEMP
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BNE FPRINT_SIGNED
        LDA #'0'
        JSR FPRINT_CHAR
        JMP FPRINT_DONE
FPRINT_SIGNED:
        LDA B_FAC+1
        BPL FPRINT_MAGNITUDE
        LDA #'-'
        JSR FPRINT_CHAR
        JSR FABS
FPRINT_MAGNITUDE:
        LDA #0
        STA B_FEXP
FPRINT_UP:
        LDX #CON_TEN_I
        JSR COPYCON
        JSR FCOMP
        BCC FPRINT_DOWN
        JSR FDIV
        INC B_FEXP
        LDA B_FEXP
        CMP #40
        BCS FPRINT_DIGITS
        JMP FPRINT_UP
FPRINT_DOWN:
        LDX #CON_ONE_I
        JSR COPYCON
        JSR FCOMP
        BCS FPRINT_DIGITS
        LDX #CON_TEN_I
        JSR COPYCON
        JSR FMUL
        DEC B_FEXP
        LDA B_FEXP
        CMP #$D8
        BCC FPRINT_DIGITS
        JMP FPRINT_DOWN

FPRINT_DIGITS:
        LDA #0
        STA B_FCOUNT
FPRINT_DLOOP:
        JSR FTOI
        LDY B_FCOUNT
        STA B_DIGITBUF,Y
        JSR FSUBDIG
        JSR FMUL10
        INC B_FCOUNT
        LDA B_FCOUNT
        CMP #10
        BNE FPRINT_DLOOP
        LDA B_DIGITBUF+9
        CMP #5
        BCC FPRINT_TRIMMED
        LDX #8
FPRINT_ROUND:
        LDA B_DIGITBUF,X
        CMP #9
        BNE FPRINT_ROUND_UP
        LDA #0
        STA B_DIGITBUF,X
        DEX
        BPL FPRINT_ROUND
        LDA #1
        STA B_DIGITBUF
        LDX #1
FPRINT_ROUND_ZERO:
        LDA #0
        STA B_DIGITBUF,X
        INX
        CPX #9
        BNE FPRINT_ROUND_ZERO
        INC B_FEXP
        JMP FPRINT_TRIMMED
FPRINT_ROUND_UP:
        CLC
        ADC #1
        STA B_DIGITBUF,X
FPRINT_TRIMMED:
        LDX #8
FPRINT_TRIM:
        LDA B_DIGITBUF,X
        BNE FPRINT_HAVE_LAST
        DEX
        BNE FPRINT_TRIM
FPRINT_HAVE_LAST:
        STX B_FLAST

        LDA B_FEXP
        BMI FPRINT_RANGE_NEG
        CMP #9
        BCS FPRINT_SCI
        JMP FPRINT_FIXED
FPRINT_RANGE_NEG:
        CMP #$FC              ; -4 is the lowest plain exponent
        BCC FPRINT_SCI
        JMP FPRINT_FIXED

FPRINT_SCI:
        LDX #0
        LDA B_DIGITBUF
        JSR FPRINT_DIGIT_CHAR
        LDA B_FLAST
        BEQ FPRINT_SCI_NO_POINT
        LDA #'.'
        JSR FPRINT_CHAR
        LDX #1
FPRINT_SCI_LOOP:
        CPX B_FLAST
        BCC FPRINT_SCI_EMIT
        BEQ FPRINT_SCI_EMIT
        JMP FPRINT_SCI_NO_POINT
FPRINT_SCI_EMIT:
        LDA B_DIGITBUF,X
        JSR FPRINT_DIGIT_CHAR
        INX
        JMP FPRINT_SCI_LOOP
FPRINT_SCI_NO_POINT:
        LDA #'E'
        JSR FPRINT_CHAR
        LDA B_FEXP
        BPL FPRINT_SCI_PLUS
        LDA #'-'
        JSR FPRINT_CHAR
        LDA #0
        SEC
        SBC B_FEXP
        JMP FPRINT_SCI_EXP
FPRINT_SCI_PLUS:
        LDA #'+'
        JSR FPRINT_CHAR
        LDA B_FEXP
FPRINT_SCI_EXP:
        JSR FPRINT_EXP2
        JMP FPRINT_DONE

FPRINT_FIXED:
        LDA B_FEXP
        BPL FPRINT_FIX_POS
        LDA #'0'
        JSR FPRINT_CHAR
        LDA #'.'
        JSR FPRINT_CHAR
        LDA B_FEXP
        EOR #$FF
        STA B_FTMP
FPRINT_FIX_LEAD:
        LDA B_FTMP
        BEQ FPRINT_FIX_TAIL
        LDA #'0'
        JSR FPRINT_CHAR
        DEC B_FTMP
        JMP FPRINT_FIX_LEAD
FPRINT_FIX_TAIL:
        LDA #0
        STA B_FTMP
        JMP FPRINT_TAIL

FPRINT_FIX_POS:
        LDA #0
        STA B_FTMP
FPRINT_FIX_INT:
        LDA B_FTMP
        CMP B_FEXP
        BEQ FPRINT_FIX_LASTD
        BCS FPRINT_FIX_PAD
        LDX B_FTMP
        LDA B_DIGITBUF,X
        JSR FPRINT_DIGIT_CHAR
        INC B_FTMP
        JMP FPRINT_FIX_INT
FPRINT_FIX_LASTD:
        LDX B_FTMP
        LDA B_DIGITBUF,X
        JSR FPRINT_DIGIT_CHAR
        INC B_FTMP
        JMP FPRINT_FIX_POINT
FPRINT_FIX_PAD:
        LDA #'0'
        JSR FPRINT_CHAR
        INC B_FTMP
        JMP FPRINT_FIX_INT
FPRINT_FIX_POINT:
        LDA B_FLAST
        CMP B_FTMP
        BCC FPRINT_DONE
        LDA #'.'
        JSR FPRINT_CHAR
FPRINT_TAIL:
        LDX B_FTMP
        CPX B_FLAST
        BCC FPRINT_TAIL_EMIT
        BEQ FPRINT_TAIL_EMIT
        JMP FPRINT_DONE
FPRINT_TAIL_EMIT:
        LDA B_DIGITBUF,X
        JSR FPRINT_DIGIT_CHAR
        INC B_FTMP
        JMP FPRINT_TAIL

FPRINT_DONE:
        LDA #0
        LDY B_TEMP
        LDX B_TEMP
        STA B_NUMBUF,X
        RTS

FPRINT_CHAR:
        LDY B_TEMP
        STA B_NUMBUF,Y
        INC B_TEMP
        RTS

FPRINT_DIGIT_CHAR:
        ORA #$30
        JMP FPRINT_CHAR

FPRINT_EXP2:
        LDX #0
FPRINT_EXP2_LOOP:
        CMP #10
        BCC FPRINT_EXP2_LAST
        SBC #10
        INX
        JMP FPRINT_EXP2_LOOP
FPRINT_EXP2_LAST:
        STA B_FTMP
        TXA
        BEQ FPRINT_EXP2_UNITS
        ORA #$30
        JSR FPRINT_CHAR
FPRINT_EXP2_UNITS:
        LDA B_FTMP
        ORA #$30
        JMP FPRINT_CHAR

; ---------------------------------------------------------------------------
; FVAL: a number at (B_IPTR).
; ---------------------------------------------------------------------------

FVAL_ADV:
        INC B_IPTR
        BNE FVAL_ADV_DONE
        INC B_IPTR+1
FVAL_ADV_DONE:
        RTS

FVAL_SKIP:
        LDY #0
        LDA (B_IPTR),Y
        CMP #' '
        BNE FVAL_SKIP_DONE
        JSR FVAL_ADV
        JMP FVAL_SKIP
FVAL_SKIP_DONE:
        RTS

FVAL:
        LDA B_IPTR
        STA B_TMPP
        LDA B_IPTR+1
        STA B_TMPP+1
        JSR FZERO
        LDA #0
        STA B_FSIGN
        STA B_FDIG
        STA B_FPOINT
        STA B_FSEEN
        JSR FVAL_SKIP
        LDY #0
        LDA (B_IPTR),Y
        CMP #'-'
        BNE FVAL_MAYBEPLUS
        LDA #$80
        STA B_FSIGN
        JSR FVAL_ADV
        JMP FVAL_DIGITLOOP
FVAL_MAYBEPLUS:
        CMP #'+'
        BNE FVAL_DIGITLOOP
        JSR FVAL_ADV
FVAL_DIGITLOOP:
        LDY #0
        LDA (B_IPTR),Y
        CMP #'0'
        BCC FVAL_AFTERINT
        CMP #':'
        BCS FVAL_AFTERINT
        SEC
        SBC #'0'
        AND #$0F
        STA B_FTMP
        JSR FMUL10
        LDA B_FTMP
        JSR FADDDIG
        INC B_FSEEN
        LDA B_FPOINT
        BEQ FVAL_NOCNT
        INC B_FDIG
FVAL_NOCNT:
        JSR FVAL_ADV
        JMP FVAL_DIGITLOOP
FVAL_AFTERINT:
        LDY #0
        LDA (B_IPTR),Y
        CMP #'.'
        BNE FVAL_EXPONENT
        LDA B_FPOINT
        BNE FVAL_EXPONENT
        INC B_FPOINT
        JSR FVAL_ADV
        JMP FVAL_DIGITLOOP
FVAL_EXPONENT:
        LDA B_FSEEN
        BEQ FVAL_FAIL
        LDA B_FDIG
        BEQ FVAL_EXP_E
        STA B_FCOUNT
FVAL_FRACLOOP:
        JSR FDIV10
        DEC B_FCOUNT
        BNE FVAL_FRACLOOP
FVAL_EXP_E:
        LDY #0
        LDA (B_IPTR),Y
        CMP #'E'
        BEQ FVAL_TAKEEXP
        CMP #'e'
        BNE FVAL_SUCCESS
FVAL_TAKEEXP:
        JSR FVAL_ADV
        LDA #0
        STA B_FEXP            ; the exponent is positive
        LDY #0
        LDA (B_IPTR),Y
        CMP #'-'
        BNE FVAL_EXPPLUS
        LDA #1
        STA B_FEXP
        JSR FVAL_ADV
        JMP FVAL_EXPDIGITS
FVAL_EXPPLUS:
        CMP #'+'
        BNE FVAL_EXPDIGITS
        JSR FVAL_ADV
FVAL_EXPDIGITS:
        LDA #0
        STA B_FCOUNT
        STA B_FSEEN
FVAL_EXPLOOP:
        LDY #0
        LDA (B_IPTR),Y
        CMP #'0'
        BCC FVAL_EXPDONE
        CMP #':'
        BCS FVAL_EXPDONE
        SEC
        SBC #'0'
        STA B_FTMP
        LDA B_FCOUNT
        ASL A
        ASL A
        CLC
        ADC B_FCOUNT
        ASL A
        CLC
        ADC B_FTMP
        STA B_FCOUNT
        INC B_FSEEN
        JSR FVAL_ADV
        JMP FVAL_EXPLOOP
FVAL_EXPDONE:
        LDA B_FSEEN
        BEQ FVAL_FAIL
FVAL_APPLYEXP:
        LDA B_FCOUNT
        BEQ FVAL_SUCCESS
        LDA B_FEXP
        BNE FVAL_EXPDIV
        JSR FMUL10
        DEC B_FCOUNT
        JMP FVAL_APPLYEXP
FVAL_EXPDIV:
        JSR FDIV10
        DEC B_FCOUNT
        JMP FVAL_APPLYEXP
FVAL_SUCCESS:
        LDA B_FSIGN
        BEQ FVAL_OK
        JSR FNEG
FVAL_OK:
        CLC
        RTS
FVAL_FAIL:
        LDA B_TMPP
        STA B_IPTR
        LDA B_TMPP+1
        STA B_IPTR+1
        SEC
        RTS
