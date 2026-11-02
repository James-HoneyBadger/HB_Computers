; SPDX-License-Identifier: MIT
;
; func.inc -- the functions. A name at (B_IPTR) is offered to the table here
; before it is treated as a variable, so SQR(2) is a call and S is a variable.

; ---------------------------------------------------------------------------
; Dispatch
; ---------------------------------------------------------------------------

; FNCALL: carry set when the name was a function, which has been run and left
; its value in FAC. Carry clear when the name is not in the table.
FNCALL:
        JSR SKIPSP
        LDA #<KW_FN
        LDY #>KW_FN
        JSR TRY
        BCS FNCALL_USER         ; FN names one of the user's definitions
        JSR SKIPSP
        JSR CURCHAR
        JSR UPPER
        CMP #'A'
        BCC FNCALL_NONE
        CMP #'Z'+1
        BCS FNCALL_NONE
        LDA B_IPTR
        STA B_TMP3
        LDA B_IPTR+1
        STA B_TMP3+1
        LDX #0
FNCALL_TRY:
        LDA FN_TABLE,X
        BEQ FNCALL_MISS
        STA ZP_PTR2
        LDA FN_TABLE+1,X
        STA ZP_PTR2+1
        JSR MATCHKW
        BCS FNCALL_HIT
        LDA B_TMP3
        STA B_IPTR
        LDA B_TMP3+1
        STA B_IPTR+1
        TXA
        CLC
        ADC #4
        TAX
        JMP FNCALL_TRY
FNCALL_MISS:
        LDA B_TMP3
        STA B_IPTR
        LDA B_TMP3+1
        STA B_IPTR+1
FNCALL_NONE:
        CLC
        RTS
FNCALL_HIT:
        ; Each routine parses its own argument. Carry is the answer to the
        ; caller, and the arithmetic inside a function is free to set it, so it
        ; is set deliberately here.
        LDA FN_TABLE+2,X
        STA B_ADDR
        LDA FN_TABLE+3,X
        STA B_ADDR+1
        JSR STRCLR              ; a function starts out answering with a number
        JSR FNCALL_APPLY
        SEC
        RTS
FNCALL_APPLY:
        JMP (B_ADDR)

; FN_ONEPAREN: the argument of a function is one expression in parentheses.
FN_ONEPAREN:
        JSR SKIPSP
        JSR CURCHAR
        CMP #'('
        BNE ERR_SYNTAX
        JSR ADV
        JSR EXPR
        JSR SKIPSP
        JSR CURCHAR
        CMP #')'
        BNE ERR_SYNTAX
        JMP ADV

; ---------------------------------------------------------------------------
; The functions themselves
; ---------------------------------------------------------------------------

FN_ABS:
        JSR FN_ONEPAREN
        JMP FABS

FN_INT:
        JSR FN_ONEPAREN
        JMP FINT

FN_SGN:
        JSR FN_ONEPAREN
        JMP FSGN

FN_SQR:
        JSR FN_ONEPAREN
        LDA B_FAC+1
        BMI ERR_ILLEGAL_QUANTITY
        JMP FSQR

; RND takes an argument for the sake of the programs that write RND(1) and
; ignores it, because the register underneath is a free-running counter.
FN_RND:
        JSR FN_ONEPAREN
        LDA IO_RANDOM
        LDY #0
        JSR ITOF
        LDA #121                ; 2^-8
        STA B_ARG
        LDA #0
        STA B_ARG+1
        STA B_ARG+2
        STA B_ARG+3
        STA B_ARG+4
        JMP FMUL

FN_PEEK:
        JSR FN_ONEPAREN
        JSR FTOI
        BCS ERR_ILLEGAL_QUANTITY
        STA B_TMP
        STY B_TMP+1
        LDY #0
        LDA (B_TMP),Y
        LDY #0
        JMP ITOF

; ---------------------------------------------------------------------------
; The table: the name, then the routine. The names are in the order of the
; table, and a zero name ends it.
; ---------------------------------------------------------------------------

FN_TABLE:
        .word FN_N_ABS, FN_ABS
        .word FN_N_INT, FN_INT
        .word FN_N_SGN, FN_SGN
        .word FN_N_SQR, FN_SQR
        .word FN_N_RND, FN_RND
        .word FN_N_PEEK, FN_PEEK
        .word FN_N_EXP, FN_EXP
        .word FN_N_LOG, FN_LOG
        .word FN_N_SIN, FN_SIN
        .word FN_N_COS, FN_COS
        .word FN_N_TAN, FN_TAN
        .word FN_N_ATN, FN_ATN
        .word FN_N_LEN, FN_LEN
        .word FN_N_ASC, FN_ASC
        .word FN_N_VAL, FN_VAL
        .word FN_N_CHR, FN_CHR
        .word FN_N_STR, FN_STR
        .word FN_N_LEFT, FN_LEFT
        .word FN_N_RIGHT, FN_RIGHT
        .word FN_N_MID, FN_MID
        .byte 0

FN_N_ABS:  .asciiz "ABS"
FN_N_INT:  .asciiz "INT"
FN_N_SGN:  .asciiz "SGN"
FN_N_SQR:  .asciiz "SQR"
FN_N_RND:  .asciiz "RND"
FN_N_PEEK: .asciiz "PEEK"
FN_N_EXP:  .asciiz "EXP"
FN_N_LOG:  .asciiz "LOG"
FN_N_SIN:  .asciiz "SIN"
FN_N_COS:  .asciiz "COS"
FN_N_TAN:  .asciiz "TAN"
FN_N_ATN:  .asciiz "ATN"
FN_N_LEN:  .asciiz "LEN"
FN_N_ASC:  .asciiz "ASC"
FN_N_VAL:  .asciiz "VAL"
FN_N_CHR:  .asciiz "CHR$"
FN_N_STR:  .asciiz "STR$"
FN_N_LEFT: .asciiz "LEFT$"
FN_N_RIGHT: .asciiz "RIGHT$"
FN_N_MID:  .asciiz "MID$"

; ---------------------------------------------------------------------------
; Transcendentals
; ---------------------------------------------------------------------------
; Five-byte numbers live in FAC, ARG and four scratch slots. B_TMPP points at
; whichever slot or constant is being read or written.

SETPTR:
        STA B_TMPP
        STY B_TMPP+1
        RTS

FACFROM:
        LDY #0
        LDA (B_TMPP),Y
        STA B_FAC
        INY
        LDA (B_TMPP),Y
        STA B_FAC+1
        INY
        LDA (B_TMPP),Y
        STA B_FAC+2
        INY
        LDA (B_TMPP),Y
        STA B_FAC+3
        INY
        LDA (B_TMPP),Y
        STA B_FAC+4
        RTS

ARGFROM:
        LDY #0
        LDA (B_TMPP),Y
        STA B_ARG
        INY
        LDA (B_TMPP),Y
        STA B_ARG+1
        INY
        LDA (B_TMPP),Y
        STA B_ARG+2
        INY
        LDA (B_TMPP),Y
        STA B_ARG+3
        INY
        LDA (B_TMPP),Y
        STA B_ARG+4
        RTS

FACTO:
        LDY #0
        LDA B_FAC
        STA (B_TMPP),Y
        INY
        LDA B_FAC+1
        STA (B_TMPP),Y
        INY
        LDA B_FAC+2
        STA (B_TMPP),Y
        INY
        LDA B_FAC+3
        STA (B_TMPP),Y
        INY
        LDA B_FAC+4
        STA (B_TMPP),Y
        RTS

; TERM_STEP: one turn of a Horner chain. FAC = FAC * (B_SCR1) + the next
; constant, and B_TMPP2 walks a run of five-byte constants.
TERM_STEP:
        LDA #<B_SCR1
        LDY #>B_SCR1
        JSR SETPTR
        JSR ARGFROM
        JSR FMUL
        LDA B_TMPP2
        STA B_TMPP
        LDA B_TMPP2+1
        STA B_TMPP+1
        JSR ARGFROM
        JSR FADD
        CLC
        LDA B_TMPP2
        ADC #5
        STA B_TMPP2
        BCC TERM_STEP_DONE
        INC B_TMPP2+1
TERM_STEP_DONE:
        RTS

; TERM_RUN: run a whole chain, with the first constant in FAC and its tail in
; the table at A (low) and Y (high), B_LOOPCNT entries long.
TERM_RUN:
        ; B_TMPP already points at the first constant; the chain that follows it
        ; is at A (low) and Y (high).
        STA B_TMPP2
        STY B_TMPP2+1
        JSR FACFROM
TERM_RUN_LOOP:
        JSR TERM_STEP
        DEC B_LOOPCNT
        BNE TERM_RUN_LOOP
        RTS

; FROUND: FAC = the nearest whole number, a half going away from zero.
FROUND:
        LDA B_FAC+1
        BPL FROUND_UP
        JSR FNEG
        LDA #<C_HALF
        LDY #>C_HALF
        JSR SETPTR
        JSR ARGFROM
        JSR FADD
        JSR FINT
        JMP FNEG
FROUND_UP:
        LDA #<C_HALF
        LDY #>C_HALF
        JSR SETPTR
        JSR ARGFROM
        JSR FADD
        JMP FINT

; ---------------------------------------------------------------------------
; EXP
; ---------------------------------------------------------------------------
; exp(x) = 2^k * exp(r), with k the nearest whole number of ln2 steps and r
; the remainder, which the series then handles over a small range.

FN_EXP:
        JSR FN_ONEPAREN
EXP_BODY:
        LDA #<B_SCR2
        LDY #>B_SCR2
        JSR SETPTR
        JSR FACTO                ; keep x
        LDA #<C_LN2
        LDY #>C_LN2
        JSR SETPTR
        JSR ARGFROM
        JSR FDIV                 ; FAC = x / ln2
        JSR FROUND               ; k, the nearest whole number of ln2 steps
        LDA #<B_SCR3
        LDY #>B_SCR3
        JSR SETPTR
        JSR FACTO
        JSR FTOI
        BCC EXP_K_INT
        ; Too big for sixteen bits either way: which end depends on the sign
        ; of x, which was kept in B_SCR2 while all this was computed.
        LDA B_SCR2+1
        BMI EXP_UNDERFLOW
        JMP EXP_OVERFLOW
EXP_K_INT:
        STA B_INT2
        STY B_INT2+1
        ; The scale factor has to fit in the exponent, which is one byte, so
        ; it may run from -127 to 126. Above that the answer is too big and
        ; below it the answer is too small -- which is zero, not an error.
        LDA B_INT2+1
        BPL EXP_K_POSITIVE
        CMP #$FF
        BNE EXP_UNDERFLOW
        LDA B_INT2
        CMP #$81
        BCC EXP_UNDERFLOW
        JMP EXP_K_OK
EXP_K_POSITIVE:
        BNE EXP_OVERFLOW        ; a high byte of one or more: past 255
        LDA B_INT2
        CMP #127
        BCS EXP_OVERFLOW
        JMP EXP_K_OK
EXP_K_OK:
        ; r = x - k*ln2
        LDA #<B_SCR3
        LDY #>B_SCR3
        JSR SETPTR
        JSR FACFROM
        LDA #<C_LN2
        LDY #>C_LN2
        JSR SETPTR
        JSR ARGFROM
        JSR FMUL                 ; k*ln2
        JSR FAC2ARG
        LDA #<B_SCR2
        LDY #>B_SCR2
        JSR SETPTR
        JSR FACFROM
        JSR FSUB                 ; r
        LDA #<B_SCR1
        LDY #>B_SCR1
        JSR SETPTR
        JSR FACTO                ; the chain multiplies by r
        LDA #<C_1_5040
        LDY #>C_1_5040
        JSR SETPTR
        LDA #<EXP_TERMS
        LDY #>EXP_TERMS
        LDX #EXP_COUNT
        STX B_LOOPCNT
        JSR TERM_RUN
        ; scale by 2^k
        LDA B_FAC
        CLC
        ADC B_INT2
        STA B_FAC
        RTS
EXP_UNDERFLOW:
        ; e^-90 is smaller than this format holds, and zero is what it has to
        ; come back as: the same rule the arithmetic uses for any result that
        ; falls off the bottom.
        JMP FZERO
EXP_OVERFLOW:
        LDA #E_OVERFLOW
        JMP ERROR

; ---------------------------------------------------------------------------
; LOG
; ---------------------------------------------------------------------------
; log(x) = n*ln2 + ln(m), where x = m * 2^n and 0.5 <= m < 1. The series is
; the atanh one, ln(m) = 2*(y + y^3/3 + y^5/5 + ...), y = (m-1)/(m+1), and
; |y| <= 1/3 over that range of m.

FN_LOG:
        JSR FN_ONEPAREN
LOG_BODY:
        LDA B_FAC
        BEQ LOG_BAD
        LDA B_FAC+1
        BMI LOG_BAD
        LDA B_FAC
        SEC
        SBC #128
        STA B_INT2               ; n
        LDA #128
        STA B_FAC                ; m alone, between a half and one
        LDA #<B_SCR3
        LDY #>B_SCR3
        JSR SETPTR
        JSR FACTO
        ; y = (m-1)/(m+1)
        LDA #<C_ONE
        LDY #>C_ONE
        JSR SETPTR
        JSR ARGFROM
        JSR FSUB                 ; m-1
        LDA #<B_SCR1
        LDY #>B_SCR1
        JSR SETPTR
        JSR FACTO
        LDA #<B_SCR3
        LDY #>B_SCR3
        JSR SETPTR
        JSR FACFROM
        LDA #<C_ONE
        LDY #>C_ONE
        JSR SETPTR
        JSR ARGFROM
        JSR FADD                 ; m+1
        JSR FAC2ARG
        LDA #<B_SCR1
        LDY #>B_SCR1
        JSR SETPTR
        JSR FACFROM
        JSR FDIV                 ; y
        LDA #<B_SCR3
        LDY #>B_SCR3
        JSR SETPTR
        JSR FACTO                ; keep y
        JSR FAC2ARG
        JSR FMUL                 ; t = y*y
        LDA #<B_SCR1
        LDY #>B_SCR1
        JSR SETPTR
        JSR FACTO                ; the chain multiplies by t
        LDA #<C_1_13
        LDY #>C_1_13
        JSR SETPTR
        LDA #<LOG_TERMS
        LDY #>LOG_TERMS
        LDX #LOG_COUNT
        STX B_LOOPCNT
        JSR TERM_RUN
        LDA #<B_SCR3
        LDY #>B_SCR3
        JSR SETPTR
        JSR ARGFROM
        JSR FMUL                 ; y*(...)
        JSR FAC2ARG
        JSR FADD                 ; twice that
        LDA #<B_SCR1
        LDY #>B_SCR1
        JSR SETPTR
        JSR FACTO                ; ln(m)
        ; n*ln2
        LDA B_INT2
        LDY #0
        CMP #$80
        BCC LOG_N_POSITIVE
        LDY #$FF
LOG_N_POSITIVE:
        JSR ITOF
        LDA #<C_LN2
        LDY #>C_LN2
        JSR SETPTR
        JSR ARGFROM
        JSR FMUL
        JSR FAC2ARG
        LDA #<B_SCR1
        LDY #>B_SCR1
        JSR SETPTR
        JSR FACFROM
        JMP FADD
LOG_BAD:
        LDA #E_ILLEGAL_QUANTITY
        JMP ERROR

; ---------------------------------------------------------------------------
; The power operator
; ---------------------------------------------------------------------------
; x^y is exp(y*log(x)); a negative x is allowed when y is a whole number, and
; the sign follows from whether that number is odd.

; ARGTO: the number at (B_TMPP) becomes ARG's new home.
ARGTO:
        LDY #0
        LDA B_ARG
        STA (B_TMPP),Y
        INY
        LDA B_ARG+1
        STA (B_TMPP),Y
        INY
        LDA B_ARG+2
        STA (B_TMPP),Y
        INY
        LDA B_ARG+3
        STA (B_TMPP),Y
        INY
        LDA B_ARG+4
        STA (B_TMPP),Y
        RTS

FPOW:
        ; The base goes in one slot and the exponent in another, because the
        ; logarithm and the exponential both use the first three.
        LDA #<B_SCR4
        LDY #>B_SCR4
        JSR SETPTR
        JSR ARGTO                ; the exponent
        LDA #<B_SCR5
        LDY #>B_SCR5
        JSR SETPTR
        JSR FACTO                ; the base
        LDA B_ARG
        ORA B_ARG+1
        ORA B_ARG+2
        ORA B_ARG+3
        ORA B_ARG+4
        BNE FPOW_EXP_NOT_ZERO
        LDA #<C_ONE
        LDY #>C_ONE
        JSR SETPTR
        JMP FACFROM              ; anything to the power zero is one
FPOW_EXP_NOT_ZERO:
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BNE FPOW_BASE_NOT_ZERO
        JMP FZERO                ; zero to any other power is zero
FPOW_BASE_NOT_ZERO:
        LDA #0
        STA B_SPARE2             ; bit 0 the sign, bit 1 a reciprocal
        ; A whole-number exponent of modest size is multiplied out, so that
        ; 2^10 is 1024 exactly rather than to seven figures.
        LDA #<B_SCR4
        LDY #>B_SCR4
        JSR SETPTR
        JSR FACFROM
        JSR FTOI
        BCS FPOW_LOGEVAL
        STA B_INT2
        STY B_INT2+1
        JSR ITOF
        LDA #<B_SCR4
        LDY #>B_SCR4
        JSR SETPTR
        JSR ARGFROM
        JSR FCOMP
        BNE FPOW_LOGEVAL         ; not a whole number
        LDA B_INT2+1
        BEQ FPOW_INT_SMALL
        CMP #$FF
        BNE FPOW_LOGEVAL         ; far too large to multiply out
        LDA B_INT2
        CMP #$E1                 ; -31 or closer to zero
        BCC FPOW_LOGEVAL
        JMP FPOW_INT_COUNT
FPOW_INT_SMALL:
        LDA B_INT2
        CMP #32
        BCS FPOW_LOGEVAL
FPOW_INT_COUNT:
        LDA B_INT2
        LDX B_INT2+1
        BPL FPOW_INT_COUNT_OK
        LDA #0
        SEC
        SBC B_INT2               ; the size of a negative exponent
        PHA
        LDA B_SPARE2
        ORA #2
        STA B_SPARE2
        PLA
FPOW_INT_COUNT_OK:
        STA B_LOOPCNT
        LDA #<B_SCR5
        LDY #>B_SCR5
        JSR SETPTR
        JSR FACFROM              ; the base, whose sign is what matters now
        LDA B_FAC+1
        BPL FPOW_INT_MULTIPLY
        LDA B_INT2
        AND #1
        BEQ FPOW_INT_MULTIPLY
        LDA #1
        STA B_SPARE2             ; an odd power keeps the sign
FPOW_INT_MULTIPLY:
        LDA #<C_ONE
        LDY #>C_ONE
        JSR SETPTR
        JSR FACFROM
        LDA #<B_SCR5
        LDY #>B_SCR5
        JSR SETPTR
        JSR ARGFROM
FPOW_INT_LOOP:
        JSR FMUL
        DEC B_LOOPCNT
        BNE FPOW_INT_LOOP
        LDA B_SPARE2
        AND #2
        BEQ FPOW_INT_SIGN
        JSR FAC2ARG
        LDA #<C_ONE
        LDY #>C_ONE
        JSR SETPTR
        JSR FACFROM
        JSR FDIV                 ; a negative power is the reciprocal
FPOW_INT_SIGN:
        LDA B_SPARE2
        AND #1
        BEQ FPOW_DONE
        JSR FNEG
        RTS

FPOW_LOGEVAL:
        LDA B_FAC+1
        BPL FPOW_COMPUTE
        ; A negative base needs a whole-number exponent to mean anything, and
        ; the test above has already turned everything else away.
        LDA B_INT2
        AND #1
        BEQ FPOW_COMPUTE
        LDA #1
        STA B_SPARE2
FPOW_COMPUTE:
        LDA #<B_SCR5
        LDY #>B_SCR5
        JSR SETPTR
        JSR FACFROM              ; the base
        LDA B_FAC+1
        BPL FPOW_BASE_ABS
        JSR FABS
FPOW_BASE_ABS:
        JSR LOG_BODY             ; log of its size
        LDA #<B_SCR4
        LDY #>B_SCR4
        JSR SETPTR
        JSR ARGFROM
        JSR FMUL                 ; times the exponent
        JSR EXP_BODY
        LDA B_SPARE2
        BEQ FPOW_DONE
        JSR FNEG
FPOW_DONE:
        RTS
FPOW_ILLEGAL:
        LDA #E_ILLEGAL_QUANTITY
        JMP ERROR

; ---------------------------------------------------------------------------
; SIN and COS
; ---------------------------------------------------------------------------
; The angle is reduced to a whole number of turns, then folded into a quarter
; turn, where a short series is plenty.

FN_SIN:
        JSR FN_ONEPAREN
        JMP SIN_FROM_FAC

FN_COS:
        JSR FN_ONEPAREN
        LDA #<C_PI2
        LDY #>C_PI2
        JSR SETPTR
        JSR ARGFROM
        JSR FADD                 ; cos x is sin(x + pi/2)
        JMP SIN_FROM_FAC

; SIN_FROM_FAC: FAC = the sine of the radians in FAC.
SIN_FROM_FAC:
        LDA #<B_SCR2
        LDY #>B_SCR2
        JSR SETPTR
        JSR FACTO                ; keep the angle
        LDA #<C_TWOPI
        LDY #>C_TWOPI
        JSR SETPTR
        JSR ARGFROM
        JSR FDIV                 ; x / 2pi
        JSR FROUND               ; whole turns
        LDA #<B_SCR3
        LDY #>B_SCR3
        JSR SETPTR
        JSR FACTO
        LDA #<C_TWOPI
        LDY #>C_TWOPI
        JSR SETPTR
        JSR ARGFROM
        JSR FMUL                 ; turns * 2pi
        JSR FAC2ARG
        LDA #<B_SCR2
        LDY #>B_SCR2
        JSR SETPTR
        JSR FACFROM
        JSR FSUB                 ; r, now between -pi and pi
        ; the sine is odd, so keep the sign and work on the size
        LDA #0
        STA B_SPARE2
        LDA B_FAC+1
        BPL SIN_POSITIVE
        LDA #1
        STA B_SPARE2
        JSR FNEG
SIN_POSITIVE:
        ; Past half of a turn, sin(a) = sin(pi - a).
        LDA #<C_PI2
        LDY #>C_PI2
        JSR SETPTR
        JSR ARGFROM
        JSR FCOMP
        CMP #1
        BNE SIN_UNDER_HALF
        JSR FAC2ARG
        LDA #<C_PI
        LDY #>C_PI
        JSR SETPTR
        JSR FACFROM
        JSR FSUB                 ; pi - a
SIN_UNDER_HALF:
        ; Past a quarter of a turn, sin(a) = cos(pi/2 - a), and the cosine
        ; series converges faster over what is left.
        LDA #<C_PI4
        LDY #>C_PI4
        JSR SETPTR
        JSR ARGFROM
        JSR FCOMP
        CMP #1
        BNE SIN_TAIL             ; inside a quarter turn already
        JSR FAC2ARG
        LDA #<C_PI2
        LDY #>C_PI2
        JSR SETPTR
        JSR FACFROM
        JSR FSUB                 ; pi/2 - a
        JSR SIN_COS_SERIES
        JMP SIN_SIGN
SIN_TAIL:
        JSR SIN_SIN_SERIES
SIN_SIGN:
        LDA B_SPARE2
        BEQ SIN_DONE
        JSR FNEG
SIN_DONE:
        RTS

; SIN_SIN_SERIES: FAC = the sine of FAC, which must be within a quarter turn.
SIN_SIN_SERIES:
        LDA #<B_SCR3
        LDY #>B_SCR3
        JSR SETPTR
        JSR FACTO                ; keep the angle for the last multiply
        JSR FAC2ARG
        JSR FMUL                 ; t = a*a
        LDA #<B_SCR1
        LDY #>B_SCR1
        JSR SETPTR
        JSR FACTO
        LDA #<C_1_362880
        LDY #>C_1_362880
        JSR SETPTR
        LDA #<SIN_TERMS
        LDY #>SIN_TERMS
        LDX #SIN_COUNT
        STX B_LOOPCNT
        JSR TERM_RUN
        LDA #<B_SCR3
        LDY #>B_SCR3
        JSR SETPTR
        JSR ARGFROM
        JMP FMUL

; SIN_COS_SERIES: FAC = the cosine of FAC, which must be within a quarter turn.
SIN_COS_SERIES:
        LDA #<B_SCR3
        LDY #>B_SCR3
        JSR SETPTR
        JSR FACTO
        JSR FAC2ARG
        JSR FMUL                 ; t = a*a
        LDA #<B_SCR1
        LDY #>B_SCR1
        JSR SETPTR
        JSR FACTO
        LDA #<C_1_3628800
        LDY #>C_1_3628800
        JSR SETPTR
        LDA #<COS_TERMS
        LDY #>COS_TERMS
        LDX #COS_COUNT
        STX B_LOOPCNT
        JMP TERM_RUN

; ---------------------------------------------------------------------------
; TAN
; ---------------------------------------------------------------------------

FN_TAN:
        JSR FN_ONEPAREN
        LDA #<B_SCR4
        LDY #>B_SCR4
        JSR SETPTR
        JSR FACTO                ; keep the angle
        LDA #<C_PI2
        LDY #>C_PI2
        JSR SETPTR
        JSR ARGFROM
        JSR FADD
        JSR SIN_FROM_FAC         ; the cosine
        LDA #<B_SCR5
        LDY #>B_SCR5
        JSR SETPTR
        JSR FACTO                ; keep it
        LDA #<B_SCR4
        LDY #>B_SCR4
        JSR SETPTR
        JSR FACFROM
        JSR SIN_FROM_FAC         ; the sine
        JSR FAC2ARG
        LDA #<B_SCR5
        LDY #>B_SCR5
        JSR SETPTR
        JSR FACFROM
        JMP FDIV                 ; sin / cos

; ---------------------------------------------------------------------------
; ATN
; ---------------------------------------------------------------------------
; |x| above one becomes pi/2 - atn(1/x), and the half-angle step then keeps
; the series argument under 0.42, where thirteen terms are enough.

FN_ATN:
        JSR FN_ONEPAREN
        LDA #0
        STA B_SPARE2             ; bit 0: the sign, bit 1: the reciprocal
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BEQ ATN_DONE             ; atn(0) is 0
        LDA B_FAC+1
        BPL ATN_POSITIVE
        LDA #1
        STA B_SPARE2
        JSR FNEG
ATN_POSITIVE:
        LDA #<C_ONE
        LDY #>C_ONE
        JSR SETPTR
        JSR ARGFROM
        JSR FCOMP
        CMP #1
        BNE ATN_UNDER_ONE
        ; above one: work on the reciprocal
        JSR FAC2ARG
        LDA #<C_ONE
        LDY #>C_ONE
        JSR SETPTR
        JSR FACFROM
        JSR FDIV
        LDA B_SPARE2
        ORA #2
        STA B_SPARE2
ATN_UNDER_ONE:
        ; the half-angle step: x becomes x/(1+sqr(1+x*x))
        LDA #<B_SCR4
        LDY #>B_SCR4
        JSR SETPTR
        JSR FACTO                ; keep x
        JSR FAC2ARG
        JSR FMUL                 ; x*x
        LDA #<C_ONE
        LDY #>C_ONE
        JSR SETPTR
        JSR ARGFROM
        JSR FADD                 ; 1+x*x
        JSR FSQR
        LDA #<C_ONE
        LDY #>C_ONE
        JSR SETPTR
        JSR ARGFROM
        JSR FADD                 ; 1+sqr(1+x*x)
        JSR FAC2ARG
        LDA #<B_SCR4
        LDY #>B_SCR4
        JSR SETPTR
        JSR FACFROM
        JSR FDIV                 ; the reduced argument
        ; the series
        LDA #<B_SCR3
        LDY #>B_SCR3
        JSR SETPTR
        JSR FACTO
        JSR FAC2ARG
        JSR FMUL                 ; t
        LDA #<B_SCR1
        LDY #>B_SCR1
        JSR SETPTR
        JSR FACTO
        LDA #<C_N1_17
        LDY #>C_N1_17
        JSR SETPTR
        LDA #<ATN_TERMS
        LDY #>ATN_TERMS
        LDX #ATN_COUNT
        STX B_LOOPCNT
        JSR TERM_RUN
        LDA #<B_SCR3
        LDY #>B_SCR3
        JSR SETPTR
        JSR ARGFROM
        JSR FMUL                 ; z*(...)
        JSR FAC2ARG
        JSR FADD                 ; the half-angle doubles it
        LDA B_SPARE2
        AND #2
        BEQ ATN_SIGN
        ; pi/2 - the result
        JSR FAC2ARG
        LDA #<C_PI2
        LDY #>C_PI2
        JSR SETPTR
        JSR FACFROM
        JSR FSUB
ATN_SIGN:
        LDA B_SPARE2
        AND #1
        BEQ ATN_DONE
        JSR FNEG
ATN_DONE:
        RTS

; ---------------------------------------------------------------------------
; The constants and the chains
; ---------------------------------------------------------------------------

C_LN2:         .byte $80, $31, $72, $17, $F8
C_PI:          .byte $82, $49, $0F, $DA, $A2
C_PI2:         .byte $81, $49, $0F, $DA, $A2
C_PI4:         .byte $80, $49, $0F, $DA, $A2
C_TWOPI:       .byte $83, $49, $0F, $DA, $A2
C_HALF:        .byte $80, $00, $00, $00, $00
C_ONE:         .byte $81, $00, $00, $00, $00
C_1_5040:      .byte $74, $50, $0D, $00, $D0
C_1_362880:    .byte $6E, $38, $EF, $1D, $2B
C_1_3628800:   .byte $6B, $13, $F2, $7D, $BC
C_N1_17:       .byte $7C, $F0, $F0, $F0, $F1
C_1_9:         .byte $7D, $63, $8E, $38, $E4
C_1_13:        .byte $7D, $1D, $89, $D8, $9E

EXP_COUNT = 7
EXP_TERMS:
        .byte $77, $36, $0B, $60, $B6   ; 1/720
        .byte $7A, $08, $88, $88, $89   ; 1/120
        .byte $7C, $2A, $AA, $AA, $AB   ; 1/24
        .byte $7E, $2A, $AA, $AA, $AB   ; 1/6
        .byte $80, $00, $00, $00, $00   ; 1/2
        .byte $81, $00, $00, $00, $00   ; 1
        .byte $81, $00, $00, $00, $00   ; 1

LOG_COUNT = 6
LOG_TERMS:
        .byte $7D, $3A, $2E, $8B, $A3   ; 1/11
        .byte $7D, $63, $8E, $38, $E4   ; 1/9
        .byte $7E, $12, $49, $24, $92   ; 1/7
        .byte $7E, $4C, $CC, $CC, $CD   ; 1/5
        .byte $7F, $2A, $AA, $AA, $AB   ; 1/3
        .byte $81, $00, $00, $00, $00   ; 1

SIN_COUNT = 4
SIN_TERMS:
        .byte $74, $D0, $0D, $00, $D0   ; -1/5040
        .byte $7A, $08, $88, $88, $89   ; 1/120
        .byte $7E, $AA, $AA, $AA, $AB   ; -1/6
        .byte $81, $00, $00, $00, $00   ; 1

COS_COUNT = 5
COS_TERMS:
        .byte $71, $50, $0D, $00, $D0   ; 1/40320
        .byte $77, $B6, $0B, $60, $B6   ; -1/720
        .byte $7C, $2A, $AA, $AA, $AB   ; 1/24
        .byte $80, $80, $00, $00, $00   ; -1/2
        .byte $81, $00, $00, $00, $00   ; 1

ATN_COUNT = 7
ATN_TERMS:
        .byte $7D, $1D, $89, $D8, $9E   ; 1/13
        .byte $7D, $BA, $2E, $8B, $A3   ; -1/11
        .byte $7D, $63, $8E, $38, $E4   ; 1/9
        .byte $7E, $92, $49, $24, $92   ; -1/7
        .byte $7E, $4C, $CC, $CC, $CD   ; 1/5
        .byte $7F, $AA, $AA, $AA, $AB   ; -1/3
        .byte $81, $00, $00, $00, $00   ; 1
