; SPDX-License-Identifier: MIT
;
; basic.s -- KESTREL 16 BASIC 2.0, the language in bank one of the Kestrel 16's ROM set.
;
; The Kestrel's BASIC was squeezed into a sixteen-kilobyte socket and counted in
; eight-bit arithmetic because that is all its processor did cheaply. This one
; has a sixty-four kilobyte bank of its own and a 65816 under it, so its numbers
; are sixteen bits wide, its variables are words, and its expressions are
; evaluated on the processor's own stack.
;
; What is here: the line reader, the statement dispatcher, integer expressions
; with the four arithmetic operators and parentheses, variables A to Z, PRINT
; with strings and separators, LET, CLS, REM, END and NEW. What is not here yet
; -- the stored program, and the control flow that needs one -- is the next
; thing to go in.

        .include "kestrel16.inc"

        .org BASIC

; The language's own direct page, above the KERNAL's.
ZP_SRC          = $30           ; a 24-bit pointer into the line being read
ZP_KW           = $33           ; a 24-bit pointer to a keyword
ZP_SAVE         = $36           ; a 24-bit pointer, saved and restored
ZP_VAL          = $39           ; 16-bit: the value being built
ZP_A            = $3B           ; 16-bit operand
ZP_B            = $3D           ; 16-bit operand
ZP_CNT          = $3F           ; 16-bit counter
ZP_CH           = $41           ; the character under the pointer
ZP_FLAG         = $42           ; PRINT's separator state
ZP_VAR          = $43           ; which variable an assignment is writing

VARS            = $0007C0       ; twenty-six words, A to Z

ENTRY:
        SEP #$20
        REP #$10
        LDA #LIGHTGREY
        STA IO_INK
        LDA #BLACK
        STA IO_PAPER
        JSL L_CLS
        JSR SAY_BANNER
MAIN_LOOP:
        JSR SAY_NEWLINE
        JSR SAY_READY
        JSR SAY_PROMPT
        LDA #<LINEBUF
        STA ZP_PTR
        LDA #>LINEBUF
        STA ZP_PTR+1
        LDA #BANK_KERNAL
        STA ZP_PTR+2
        JSL L_GETLINE
        LDA #<LINEBUF
        STA ZP_SRC
        LDA #>LINEBUF
        STA ZP_SRC+1
        LDA #BANK_KERNAL
        STA ZP_SRC+2
        JSR EXEC_LINE
        BRA MAIN_LOOP

; ---------------------------------------------------------------------------
; The messages
; ---------------------------------------------------------------------------

TXT_BANNER:
        .text "KESTREL 16 BASIC 2.0"
TXT_BANNER_END:
TXT_READY:
        .text "READY"
TXT_READY_END:
TXT_PROMPT:
        .text "> "
TXT_PROMPT_END:
TXT_WHAT:
        .text "?WHAT?"
TXT_WHAT_END:
KW_PRINT:
        .text "PRINT"
KW_PRINT_END:
KW_LET:
        .text "LET"
KW_LET_END:
KW_CLS:
        .text "CLS"
KW_CLS_END:
KW_REM:
        .text "REM"
KW_REM_END:
KW_END:
        .text "END"
KW_END_END:
KW_NEW:
        .text "NEW"
KW_NEW_END:

SAY_BANNER:
        LDA #<TXT_BANNER
        STA ZP_PTR
        LDA #>TXT_BANNER
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_BANNER_END - TXT_BANNER
        JSL L_PRINT
        RTS
SAY_READY:
        LDA #<TXT_READY
        STA ZP_PTR
        LDA #>TXT_READY
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_READY_END - TXT_READY
        JSL L_PRINT
        RTS
SAY_PROMPT:
        LDA #<TXT_PROMPT
        STA ZP_PTR
        LDA #>TXT_PROMPT
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_PROMPT_END - TXT_PROMPT
        JSL L_PRINT
        RTS
SAY_NEWLINE:
        LDA #KEY_RETURN
        JSL L_CHROUT
        RTS
SAY_WHAT:
        LDA #<TXT_WHAT
        STA ZP_PTR
        LDA #>TXT_WHAT
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_WHAT_END - TXT_WHAT
        JSL L_PRINT
        RTS

; ---------------------------------------------------------------------------
; Reading the line
; ---------------------------------------------------------------------------

SKIPSP:
        PHP
        SEP #$20
SKIPSP_LOOP:
        LDA [ZP_SRC]
        CMP #32
        BNE SKIPSP_DONE
        INC ZP_SRC
        BRA SKIPSP_LOOP
SKIPSP_DONE:
        PLP
        RTS

TO_UPPER:
        CMP #'a'
        BCC TO_UPPER_DONE
        CMP #'z'+1
        BCS TO_UPPER_DONE
        SEC
        SBC #32
TO_UPPER_DONE:
        RTS

; Carry set when the character under the pointer is a letter.
IS_ALPHA:
        PHP
        SEP #$20
        LDA [ZP_SRC]
        JSR TO_UPPER
        CMP #'A'
        BCC IS_ALPHA_NO
        CMP #'Z'+1
        BCS IS_ALPHA_NO
        PLP
        SEC
        RTS
IS_ALPHA_NO:
        PLP
        CLC
        RTS

; MATCHKW: X = keyword, Y = its length. Carry set when the line starts with it,
; and the line pointer has stepped past the keyword and any spaces after it.
MATCHKW:
        SEP #$20
        STX ZP_KW
        LDA #BANK_BASIC
        STA ZP_KW+2
        STY ZP_CNT
        LDA ZP_SRC
        STA ZP_SAVE
        LDA ZP_SRC+1
        STA ZP_SAVE+1
        LDA ZP_SRC+2
        STA ZP_SAVE+2
        LDY #0
MATCHKW_LOOP:
        CPY ZP_CNT
        BCS MATCHKW_YES
        LDA [ZP_KW],Y
        STA ZP_CH
        LDA [ZP_SAVE],Y
        JSR TO_UPPER
        CMP ZP_CH
        BNE MATCHKW_NO
        INY
        BRA MATCHKW_LOOP
MATCHKW_YES:
        REP #$20
        LDA ZP_SAVE
        CLC
        ADC ZP_CNT
        STA ZP_SRC
        SEP #$20
        LDA ZP_SAVE+2
        STA ZP_SRC+2
        JSR SKIPSP
        SEC
        RTS
MATCHKW_NO:
        CLC
        RTS

; ---------------------------------------------------------------------------
; EXEC_LINE: run the statement line at ZP_SRC.
; ---------------------------------------------------------------------------

EXEC_LINE:
        JSR SKIPSP
        LDA [ZP_SRC]
        BEQ EXEC_DONE
        LDX.W #<KW_PRINT
        LDY.W #KW_PRINT_END - KW_PRINT
        JSR MATCHKW
        BCS DO_PRINT
        LDA [ZP_SRC]
        CMP #'?'
        BNE EXEC_TRY_LET
        INC ZP_SRC
        JSR SKIPSP
        BRA DO_PRINT
EXEC_TRY_LET:
        LDX.W #<KW_LET
        LDY.W #KW_LET_END - KW_LET
        JSR MATCHKW
        BCS DO_LET
        LDX.W #<KW_CLS
        LDY.W #KW_CLS_END - KW_CLS
        JSR MATCHKW
        BCS DO_CLS
        LDX.W #<KW_REM
        LDY.W #KW_REM_END - KW_REM
        JSR MATCHKW
        BCS EXEC_DONE
        LDX.W #<KW_NEW
        LDY.W #KW_NEW_END - KW_NEW
        JSR MATCHKW
        BCS DO_NEW
        LDX.W #<KW_END
        LDY.W #KW_END_END - KW_END
        JSR MATCHKW
        BCS DO_END
        JSR IS_ALPHA
        BCS DO_LET
        JSR SAY_WHAT
        BRA EXEC_DONE
DO_NEW:
        BRA EXEC_DONE
DO_CLS:
        JSL L_CLS
        JSR SAY_BANNER
        JSR SAY_NEWLINE
        BRA EXEC_DONE
DO_END:
        JSR SAY_NEWLINE
        BRA EXEC_DONE
DO_LET:
        JSR DO_ASSIGN
        BRA EXEC_DONE
DO_PRINT:
        JSR DO_PRINT_STMT
EXEC_DONE:
        RTS

; ---------------------------------------------------------------------------
; LET: a letter, an equals sign, an expression.
; ---------------------------------------------------------------------------

DO_ASSIGN:
        JSR SKIPSP
        JSR IS_ALPHA
        BCC ASSIGN_DONE
        LDA [ZP_SRC]
        JSR TO_UPPER
        SEC
        SBC #'A'
        STA ZP_VAR              ; the expression may use ZP_CH as scratch
        INC ZP_SRC
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'='
        BEQ ASSIGN_STORE
        JSR SAY_WHAT
        RTS
ASSIGN_STORE:
        INC ZP_SRC
        JSR EXPR
        REP #$20
        LDA ZP_VAR
        AND.W #$00FF
        ASL A
        TAY
        TXA
        STA VARS,Y
        SEP #$20
ASSIGN_DONE:
        RTS

; ---------------------------------------------------------------------------
; PRINT
; ---------------------------------------------------------------------------

DO_PRINT_STMT:
        SEP #$20
        LDA #0
        STA ZP_FLAG
PRINT_LOOP:
        JSR SKIPSP
        LDA [ZP_SRC]
        BEQ PRINT_END
        CMP #'"'
        BEQ PRINT_STRING
        CMP #';'
        BEQ PRINT_SEMI
        CMP #','
        BEQ PRINT_COMMA
        JSR EXPR
        JSR PRNUM
        BRA PRINT_LOOP
PRINT_STRING:
        INC ZP_SRC
PRINT_STR_LOOP:
        LDA [ZP_SRC]
        BEQ PRINT_END
        CMP #'"'
        BEQ PRINT_STR_CLOSE
        JSL L_CHROUT
        INC ZP_SRC
        BRA PRINT_STR_LOOP
PRINT_STR_CLOSE:
        INC ZP_SRC
        BRA PRINT_LOOP
PRINT_SEMI:
        INC ZP_SRC
        LDA #1
        STA ZP_FLAG
        BRA PRINT_LOOP
PRINT_COMMA:
        INC ZP_SRC
        LDA #32
        JSL L_CHROUT
        LDA #0
        STA ZP_FLAG
        BRA PRINT_LOOP
PRINT_END:
        LDA ZP_FLAG
        BNE PRINT_DONE
        JSR SAY_NEWLINE
PRINT_DONE:
        RTS

; ---------------------------------------------------------------------------
; Expressions. The value of an expression is returned in X.
; ---------------------------------------------------------------------------

EXPR:
        JSR TERM
        PHX
EXPR_LOOP:
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'+'
        BEQ EXPR_ADD
        CMP #'-'
        BEQ EXPR_SUB
        PLX
        RTS
EXPR_ADD:
        INC ZP_SRC
        JSR TERM
        PLY
        REP #$20
        TXA
        STA ZP_VAL
        TYA
        CLC
        ADC ZP_VAL
        TAX
        SEP #$20
        PHX
        BRA EXPR_LOOP
EXPR_SUB:
        INC ZP_SRC
        JSR TERM
        PLY
        REP #$20
        TXA
        STA ZP_VAL
        TYA
        SEC
        SBC ZP_VAL
        TAX
        SEP #$20
        PHX
        BRA EXPR_LOOP

TERM:
        JSR FACTOR
        PHX
TERM_LOOP:
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'*'
        BEQ TERM_MUL
        CMP #'/'
        BEQ TERM_DIV
        PLX
        RTS
TERM_MUL:
        INC ZP_SRC
        JSR FACTOR
        PLY
        REP #$20
        TXA
        STA ZP_A
        TYA
        STA ZP_B
        JSR UMUL16
        REP #$20                ; the product is a word, so read it as one
        LDA ZP_A
        TAX
        SEP #$20
        PHX
        BRA TERM_LOOP
TERM_DIV:
        INC ZP_SRC
        JSR FACTOR
        PLY
        REP #$20
        TXA
        STA ZP_B
        TYA
        STA ZP_A
        JSR UDIV16
        REP #$20
        LDA ZP_A
        TAX
        SEP #$20
        PHX
        BRA TERM_LOOP

; ZP_A * ZP_B -> ZP_A
UMUL16:
        REP #$20
        LDA.W #0
        STA ZP_CNT
        LDY #16
UMUL_LOOP:
        LSR ZP_B
        BCC UMUL_NEXT
        LDA ZP_CNT
        CLC
        ADC ZP_A
        STA ZP_CNT
UMUL_NEXT:
        ASL ZP_A
        DEY
        BNE UMUL_LOOP
        LDA ZP_CNT
        STA ZP_A
        SEP #$20
        RTS

; ZP_A / ZP_B -> ZP_A, remainder in ZP_CNT
UDIV16:
        REP #$20
        LDA.W #0
        STA ZP_CNT
        LDY #16
UDIV_LOOP:
        ASL ZP_A
        ROL ZP_CNT
        LDA ZP_CNT
        CMP ZP_B
        BCC UDIV_NEXT
        SBC ZP_B
        STA ZP_CNT
        INC ZP_A
UDIV_NEXT:
        DEY
        BNE UDIV_LOOP
        SEP #$20
        RTS

FACTOR:
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'('
        BEQ FACTOR_PAREN
        CMP #'-'
        BEQ FACTOR_NEG
        CMP #'+'
        BEQ FACTOR_PLUS
        CMP #'0'
        BCC FACTOR_VAR
        CMP #'9'+1
        BCS FACTOR_VAR
        BRA FACTOR_NUM
FACTOR_PLUS:
        INC ZP_SRC
        BRA FACTOR
FACTOR_PAREN:
        INC ZP_SRC
        JSR EXPR
        PHX
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE FACTOR_PAREN_DONE
        INC ZP_SRC
FACTOR_PAREN_DONE:
        PLX
        RTS
FACTOR_NEG:
        INC ZP_SRC
        JSR FACTOR
        REP #$20
        TXA
        EOR.W #$FFFF
        INC A
        TAX
        SEP #$20
        RTS
FACTOR_NUM:
        REP #$20
        LDA.W #0
        STA ZP_VAL
        SEP #$20
FN_LOOP:
        LDA [ZP_SRC]
        CMP #'0'
        BCC FN_DONE
        CMP #'9'+1
        BCS FN_DONE
        SEC
        SBC #'0'
        STA ZP_CH
        REP #$20
        LDA ZP_VAL
        ASL A
        STA ZP_A
        ASL A
        ASL A
        CLC
        ADC ZP_A
        STA ZP_A
        SEP #$20
        LDA ZP_CH
        REP #$20
        AND.W #$00FF
        CLC
        ADC ZP_A
        STA ZP_VAL
        SEP #$20
        INC ZP_SRC
        BRA FN_LOOP
FN_DONE:
        REP #$20
        LDA ZP_VAL
        TAX
        SEP #$20
        RTS
FACTOR_VAR:
        JSR IS_ALPHA
        BCC FACTOR_NONE
        LDA [ZP_SRC]
        JSR TO_UPPER
        SEC
        SBC #'A'
        STA ZP_CH
        INC ZP_SRC
        LDA ZP_CH
        REP #$20
        AND.W #$00FF
        ASL A
        TAX
        LDA VARS,X
        TAX
        SEP #$20
        RTS
FACTOR_NONE:
        REP #$20
        LDA #0
        TAX
        SEP #$20
        RTS

; ---------------------------------------------------------------------------
; PRNUM: X = value, printed as signed decimal.
; ---------------------------------------------------------------------------

PRNUM:
        PHP
        SEP #$20
        REP #$10
        REP #$20
        TXA
        STA ZP_VAL
        BPL PRNUM_POSITIVE
        SEP #$20
        LDA #'-'
        JSL L_CHROUT
        REP #$20
        LDA ZP_VAL
        EOR.W #$FFFF
        INC A
        STA ZP_VAL
PRNUM_POSITIVE:
        LDA ZP_VAL
        STA ZP_A
        SEP #$20
        LDA #0
        STA ZP_CH
PRNUM_DIGITS:
        ; divide by ten: ZP_A / 10 -> ZP_A, remainder ZP_CNT
        REP #$20
        LDA.W #0
        STA ZP_CNT
        LDY #16
PRNUM_DIV:
        ASL ZP_A
        ROL ZP_CNT
        LDA ZP_CNT
        CMP.W #10
        BCC PRNUM_DIV_NEXT
        SBC.W #10
        STA ZP_CNT
        INC ZP_A
PRNUM_DIV_NEXT:
        DEY
        BNE PRNUM_DIV
        SEP #$20
        LDA ZP_CNT
        PHA
        INC ZP_CH
        REP #$20
        LDA ZP_A
        BNE PRNUM_DIGITS
PRNUM_OUT:
        SEP #$20
        PLA
        ORA #'0'
        JSL L_CHROUT
        DEC ZP_CH
        BNE PRNUM_OUT
        PLP
        RTS
