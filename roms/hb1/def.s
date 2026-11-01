; SPDX-License-Identifier: MIT
;
; def.s -- DEF FN. A definition is kept as the name, the parameter and a
; pointer at the expression, which stays where it was written in the program.
; That is why a definition typed at the prompt is worth nothing: the text it
; points at is gone by the next line.

; ---------------------------------------------------------------------------
; DEF FN A(X) = expression
; ---------------------------------------------------------------------------

DO_DEF:
        JSR SKIPSP
        LDA #<KW_FN
        LDY #>KW_FN
        JSR TRY
        BCS DEF_NAMED
        JMP ERR_SYNTAX
DEF_NAMED:
        JSR PARSENAME
        BCS ERR_SYNTAX
        LDA B_TEMP3
        CMP #T_STR
        BEQ DEF_REFUSED         ; only numbers come back from a function here
        LDA B_TEMP
        STA B_VNAME1
        LDA B_TEMP2
        STA B_VNAME2
        JSR SKIPSP
        JSR CURCHAR
        CMP #'('
        BNE ERR_SYNTAX
        JSR ADV
        JSR PARSENAME           ; the parameter
        BCS ERR_SYNTAX
        LDA B_TEMP3
        CMP #T_STR
        BEQ DEF_REFUSED
        LDA B_TEMP
        STA B_DEFP1
        LDA B_TEMP2
        STA B_DEFP2
        JSR SKIPSP
        JSR CURCHAR
        CMP #')'
        BNE ERR_SYNTAX
        JSR ADV
        JSR SKIPSP
        JSR CURCHAR
        CMP #'='
        BNE ERR_SYNTAX
        JSR ADV
        ; an entry with this name, or the first free one
        LDX #0
DEF_FIND:
        CPX #DEFMAX * DEFENT
        BCS DEF_FULL
        LDA DEFTAB,X
        BEQ DEF_CLAIM
        CMP B_VNAME1
        BNE DEF_NEXT
        LDA DEFTAB+1,X
        CMP B_VNAME2
        BEQ DEF_CLAIM
DEF_NEXT:
        TXA
        CLC
        ADC #DEFENT
        TAX
        JMP DEF_FIND
DEF_CLAIM:
        LDA B_VNAME1
        STA DEFTAB,X
        LDA B_VNAME2
        STA DEFTAB+1,X
        LDA B_DEFP1
        STA DEFTAB+2,X
        LDA B_DEFP2
        STA DEFTAB+3,X
        LDA B_IPTR
        STA DEFTAB+4,X
        LDA B_IPTR+1
        STA DEFTAB+5,X
        ; the body belongs to the definition, so the rest of the line is skipped
DEF_SKIP:
        JSR CURCHAR
        BEQ DEF_DONE
        JSR ADV
        JMP DEF_SKIP
DEF_DONE:
        CLC
        RTS
DEF_FULL:
        JMP ERR_OUT_OF_MEMORY
DEF_REFUSED:
        JMP ERR_TYPE_MISMATCH

; ---------------------------------------------------------------------------
; Calling one
; ---------------------------------------------------------------------------

; FNCALL_USER: the text after FN names a definition. The argument is worked out
; and bound to the parameter, then the stored expression is read where it lies.
FNCALL_USER:
        JSR PARSENAME
        BCS ERR_SYNTAX
        LDX #0
FU_FIND:
        CPX #DEFMAX * DEFENT
        BCS FU_MISSING
        LDA DEFTAB,X
        CMP B_TEMP
        BNE FU_NEXT
        LDA DEFTAB+1,X
        CMP B_TEMP2
        BEQ FU_FOUND
FU_NEXT:
        TXA
        CLC
        ADC #DEFENT
        TAX
        JMP FU_FIND
FU_MISSING:
        LDA #E_UNDEF_FUNCTION
        JMP ERROR
FU_FOUND:
        TXA
        PHA                     ; the entry, while the argument is worked out
        JSR STRARG_OPEN
        JSR EXPR
        JSR STRNONE
        JSR STRARG_CLOSE
        PLA
        TAX
        LDA DEFTAB+2,X
        STA B_TEMP
        LDA DEFTAB+3,X
        STA B_TEMP2
        LDA #T_NUM
        STA B_TEMP3
        LDA DEFTAB+4,X
        STA B_DEFEXPR
        LDA DEFTAB+5,X
        STA B_DEFEXPR+1
        JSR VARSET              ; the parameter takes the argument
        LDA B_IPTR              ; and the body is read where it was written
        PHA
        LDA B_IPTR+1
        PHA
        LDA B_DEFEXPR
        STA B_IPTR
        LDA B_DEFEXPR+1
        STA B_IPTR+1
        JSR EXPR
        JSR STRCLR
        PLA
        STA B_IPTR+1
        PLA
        STA B_IPTR
        SEC                     ; a function was found and run
        RTS
