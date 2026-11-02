; SPDX-License-Identifier: MIT
;
; eval.inc -- expressions: numbers, variables, operators.
;
; The grammar, from loosest to tightest:
;
;       OR  AND  (NOT)  comparisons  + -  * /  unary - +  operand
;
; Every level works the same way: parse a tighter level into FAC, and while the
; operator for this level is next, push FAC, parse another, pop the right-hand
; value into ARG, and apply. The value stack is VALSTK.

; ---------------------------------------------------------------------------
; Character plumbing
; ---------------------------------------------------------------------------

SKIPSP:
        LDY #0
        LDA (B_IPTR),Y
        CMP #' '
        BNE SKIPSP_DONE
        JSR ADV
        JMP SKIPSP
SKIPSP_DONE:
        RTS

ADV:
        INC B_IPTR
        BNE ADV_DONE
        INC B_IPTR+1
ADV_DONE:
        RTS

CURCHAR:
        LDY #0
        LDA (B_IPTR),Y
        RTS

UPPER:
        CMP #'a'
        BCC UPPER_DONE
        CMP #'z'+1
        BCS UPPER_DONE
        AND #$5F
UPPER_DONE:
        RTS

; ---------------------------------------------------------------------------
; The value stack
; ---------------------------------------------------------------------------

VALSIZE = 6
VALSTK_MAX = 16

; PUSHNUM: FAC goes on the stack.
PUSHNUM:
        LDA B_VALSP
        CMP #VALSTK_MAX
        BCS PUSH_FULL
        ASL A
        STA B_TEMP
        ASL A
        CLC
        ADC B_TEMP           ; level * 6
        TAX
        LDA #T_NUM
        STA VALSTK,X
        LDA B_FAC
        STA VALSTK+1,X
        LDA B_FAC+1
        STA VALSTK+2,X
        LDA B_FAC+2
        STA VALSTK+3,X
        LDA B_FAC+3
        STA VALSTK+4,X
        LDA B_FAC+4
        STA VALSTK+5,X
        INC B_VALSP
        RTS
PUSH_FULL:
        JMP ERR_OUT_OF_MEMORY

; POPNUM: the top of the stack becomes FAC.
POPNUM:
        LDA B_VALSP
        BEQ POP_EMPTY
        DEC B_VALSP
        LDA B_VALSP
        ASL A
        STA B_TEMP
        ASL A
        CLC
        ADC B_TEMP
        TAX
        LDA VALSTK+1,X
        STA B_FAC
        LDA VALSTK+2,X
        STA B_FAC+1
        LDA VALSTK+3,X
        STA B_FAC+2
        LDA VALSTK+4,X
        STA B_FAC+3
        LDA VALSTK+5,X
        STA B_FAC+4
        RTS
POP_EMPTY:
        JMP ERR_SYNTAX

; FAC2ARG: the accumulator becomes the argument. The right-hand side of an
; operator has just been computed into FAC; the left-hand side is what comes off
; the stack.
FAC2ARG:
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
        RTS

; ---------------------------------------------------------------------------
; Keywords
; ---------------------------------------------------------------------------

; KEYMATCH: the zero-terminated keyword at (ZP_PTR2) matches at (B_IPTR)?
; Carry set when it does, and (B_IPTR) has moved past it. This is the statement
; keywords' own matcher, so an operator is read by the same rule a statement is:
; the word has to end where the keyword does, and a failed match moves nothing.
KEYMATCH:
        JMP MATCHKW

KW_OR:   .asciiz "OR"
KW_AND:  .asciiz "AND"
KW_NOT:  .asciiz "NOT"

; ---------------------------------------------------------------------------
; EXPR: an expression into FAC.
; ---------------------------------------------------------------------------

EXPR:
        JSR EXPR_OR
        CLC
        RTS

EXPR_OR:
        JSR EXPR_AND
EXPR_OR_LOOP:
        JSR SKIPSP
        LDA #<KW_OR
        LDY #>KW_OR
        JSR TRY
        BCC EXPR_OR_DONE
        JSR PUSHNUM
        JSR EXPR_AND
        JSR FAC2ARG
        JSR POPNUM
        ; true when either side is not zero
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BNE EXPR_OR_TRUE
        LDA B_ARG
        ORA B_ARG+1
        ORA B_ARG+2
        ORA B_ARG+3
        ORA B_ARG+4
        BNE EXPR_OR_TRUE
        JSR FZERO
        JMP EXPR_OR_LOOP
EXPR_OR_TRUE:
        JSR FTRUE
        JMP EXPR_OR_LOOP
EXPR_OR_DONE:
        RTS

EXPR_AND:
        JSR EXPR_NOT
EXPR_AND_LOOP:
        JSR SKIPSP
        LDA #<KW_AND
        LDY #>KW_AND
        JSR TRY
        BCC EXPR_AND_DONE
        JSR PUSHNUM
        JSR EXPR_NOT
        JSR FAC2ARG
        JSR POPNUM
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BEQ EXPR_AND_FALSE
        LDA B_ARG
        ORA B_ARG+1
        ORA B_ARG+2
        ORA B_ARG+3
        ORA B_ARG+4
        BEQ EXPR_AND_FALSE
        JSR FTRUE
        JMP EXPR_AND_LOOP
EXPR_AND_FALSE:
        JSR FZERO
        JMP EXPR_AND_LOOP
EXPR_AND_DONE:
        RTS

EXPR_NOT:
        JSR SKIPSP
        LDA #<KW_NOT
        LDY #>KW_NOT
        JSR TRY
        BCC EXPR_NOT_PLAIN
        JSR EXPR_NOT
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BEQ EXPR_NOT_TRUE
        JSR FZERO
        RTS
EXPR_NOT_TRUE:
        JSR FTRUE
        RTS
EXPR_NOT_PLAIN:
        JMP EXPR_REL

; FTRUE / FZERO into FAC as true (-1) and false (0).
FTRUE:
        LDA #$FF
        LDY #$FF
        JMP ITOF

EXPR_REL:
        JSR EXPR_ADD
EXPR_REL_LOOP:
        JSR SKIPSP
        JSR RELOP
        BCC EXPR_REL_DONE
        PHA                     ; the operator waits on the machine stack: the
                                ; right side may use every zero page temporary,
                                ; B_TEMP3 included, since it holds the third
                                ; character of the variable it looks up
        LDA B_FLAGS
        AND #2
        BNE EXPR_REL_STRINGS
        JSR PUSHNUM
        JSR EXPR_ADD
        JSR STRNONE
        JSR FAC2ARG
        JSR POPNUM
        JSR FCOMP
        PHA                     ; the answer, which STRCLR would overwrite
        JSR STRCLR
        PLA
        TAY
        PLA                     ; the operator
        TAX
        TYA
        JMP EXPR_REL_TEST
EXPR_REL_STRINGS:
        ; A string on the left: it and the operator wait on the machine stack,
        ; which no expression can reach.
        LDA B_STRPTR
        PHA
        LDA B_STRPTR+1
        PHA
        LDA B_STRLEN
        PHA
        JSR EXPR_ADD
        LDA B_FLAGS
        AND #2
        BEQ ERR_TYPE_MISMATCH   ; a string and a number do not compare
        PLA
        STA B_STRLEN2
        PLA
        STA B_TEMPSTR+1
        PLA
        STA B_TEMPSTR
        PLA
        TAX
        JSR STRCMP
        PHA
        JSR STRCLR
        PLA
EXPR_REL_TEST:
        CPX #1
        BEQ EXPR_REL_EQ
        CPX #2
        BEQ EXPR_REL_NE
        CPX #3
        BEQ EXPR_REL_LT
        CPX #4
        BEQ EXPR_REL_GT
        CPX #5
        BEQ EXPR_REL_LE
        CMP #0
        BPL EXPR_REL_TRUE
        JMP EXPR_REL_FALSE
EXPR_REL_EQ:
        CMP #0
        BEQ EXPR_REL_TRUE
        JMP EXPR_REL_FALSE
EXPR_REL_NE:
        CMP #0
        BNE EXPR_REL_TRUE
        JMP EXPR_REL_FALSE
EXPR_REL_LT:
        CMP #0
        BMI EXPR_REL_TRUE
        JMP EXPR_REL_FALSE
EXPR_REL_GT:
        CMP #0
        BPL EXPR_REL_TRUE
        JMP EXPR_REL_FALSE
EXPR_REL_LE:
        CMP #0
        BMI EXPR_REL_TRUE
        BEQ EXPR_REL_TRUE
EXPR_REL_FALSE:
        JSR FZERO
        JMP EXPR_REL_LOOP
EXPR_REL_TRUE:
        JSR FTRUE
        JMP EXPR_REL_LOOP
EXPR_REL_DONE:
        RTS

; STRCMP: A = -1, 0 or 1 as the string at B_TEMPSTR, B_STRLEN2 compares with
; the one at B_STRPTR, B_STRLEN. Anything shorter sorts first, as it always did.
STRCMP:
        LDY #0
STRCMP_LOOP:
        CPY B_STRLEN2
        BEQ STRCMP_LEFT_END
        CPY B_STRLEN
        BEQ STRCMP_ABOVE
        LDA (B_TEMPSTR),Y
        STA B_CHAR
        LDA (B_STRPTR),Y
        CMP B_CHAR
        BCC STRCMP_ABOVE        ; the right is lower, so the left is above
        BNE STRCMP_BELOW
        INY
        JMP STRCMP_LOOP
STRCMP_LEFT_END:
        CPY B_STRLEN
        BEQ STRCMP_SAME
STRCMP_BELOW:
        LDA #$FF
        CLC
        RTS
STRCMP_ABOVE:
        LDA #1
        CLC
        RTS
STRCMP_SAME:
        LDA #0
        SEC
        RTS

; RELOP: -> A = 1 '=' 2 '<>' 3 '<' 4 '>' 5 '<=' 6 '>=', carry set.
RELOP:
        JSR CURCHAR
        CMP #'='
        BEQ RELOP_EQ
        CMP #'<'
        BEQ RELOP_LT
        CMP #'>'
        BEQ RELOP_GT
        CLC
        RTS
RELOP_EQ:
        JSR ADV
        LDA #1
        SEC
        RTS
RELOP_LT:
        JSR ADV
        JSR CURCHAR
        CMP #'='
        BEQ RELOP_LE
        CMP #'>'
        BEQ RELOP_NE
        LDA #3
        SEC
        RTS
RELOP_LE:
        JSR ADV
        LDA #5
        SEC
        RTS
RELOP_NE:
        JSR ADV
        LDA #2
        SEC
        RTS
RELOP_GT:
        JSR ADV
        JSR CURCHAR
        CMP #'='
        BEQ RELOP_GE
        LDA #4
        SEC
        RTS
RELOP_GE:
        JSR ADV
        LDA #6
        SEC
        RTS

EXPR_ADD:
        JSR EXPR_MUL
EXPR_ADD_LOOP:
        JSR SKIPSP
        JSR CURCHAR
        CMP #'+'
        BEQ EXPR_ADD_PLUS
        CMP #'-'
        BEQ EXPR_ADD_MINUS
        RTS
EXPR_ADD_PLUS:
        LDA B_FLAGS
        AND #2
        BNE ADD_STRINGS         ; a plus between strings joins them
        JSR ADV
        JSR PUSHNUM
        JSR EXPR_MUL
        JSR STRNONE
        JSR FAC2ARG
        JSR POPNUM
        JSR FADD
        JMP EXPR_ADD_LOOP

; ADD_STRINGS: the left string is on the stack, so the right one has room to be
; worked out; then both go into the heap, one after the other.
ADD_STRINGS:
        LDA B_STRPTR
        PHA
        LDA B_STRPTR+1
        PHA
        LDA B_STRLEN
        PHA
        JSR ADV
        JSR EXPR_MUL
        LDA B_FLAGS
        AND #2
        BEQ ERR_TYPE_MISMATCH
        PLA
        STA B_STRLEN2
        PLA
        STA B_TEMPSTR+1
        PLA
        STA B_TEMPSTR
        LDA B_STRLEN2
        CLC
        ADC B_STRLEN
        BCS ERR_STRING_TOO_LONG
        BEQ ADD_STR_EMPTY
        STA B_TMP
        JSR STRALLOC
        BCS ERR_OUT_OF_MEMORY
        LDA B_STREND
        STA B_TEMPSTR2
        LDA B_STREND+1
        STA B_TEMPSTR2+1
        LDA B_STRLEN2
        STA B_CHAR
ADD_STR_LEFT:
        LDA B_CHAR
        BEQ ADD_STR_RIGHT
        LDY #0
        LDA (B_TEMPSTR),Y
        STA (B_TEMPSTR2),Y
        INC B_TEMPSTR
        BNE ADD_STR_LEFT_NEXT
        INC B_TEMPSTR+1
ADD_STR_LEFT_NEXT:
        INC B_TEMPSTR2
        BNE ADD_STR_LEFT_TICK
        INC B_TEMPSTR2+1
ADD_STR_LEFT_TICK:
        DEC B_CHAR
        JMP ADD_STR_LEFT
ADD_STR_RIGHT:
        LDA B_STRLEN
        STA B_CHAR
ADD_STR_RIGHT_LOOP:
        LDA B_CHAR
        BEQ ADD_STR_JOINED
        LDY #0
        LDA (B_STRPTR),Y
        STA (B_TEMPSTR2),Y
        INC B_STRPTR
        BNE ADD_STR_RIGHT_NEXT
        INC B_STRPTR+1
ADD_STR_RIGHT_NEXT:
        INC B_TEMPSTR2
        BNE ADD_STR_RIGHT_TICK
        INC B_TEMPSTR2+1
ADD_STR_RIGHT_TICK:
        DEC B_CHAR
        JMP ADD_STR_RIGHT_LOOP
ADD_STR_JOINED:
        LDA B_STREND
        STA B_STRPTR
        LDA B_STREND+1
        STA B_STRPTR+1
        LDA B_TMP
        STA B_STRLEN
        JSR STRSET
        JMP EXPR_ADD_LOOP
ADD_STR_EMPTY:
        LDA #0
        STA B_STRLEN
        LDA #<B_STRCHR
        STA B_STRPTR
        LDA #>B_STRCHR
        STA B_STRPTR+1
        JSR STRSET
        JMP EXPR_ADD_LOOP
EXPR_ADD_MINUS:
        JSR STRNONE
        JSR ADV
        JSR PUSHNUM
        JSR EXPR_MUL
        JSR FAC2ARG
        JSR POPNUM
        JSR FSUB
        JMP EXPR_ADD_LOOP

EXPR_MUL:
        JSR EXPR_POW
EXPR_MUL_LOOP:
        JSR SKIPSP
        JSR CURCHAR
        CMP #'*'
        BEQ EXPR_MUL_TIMES
        CMP #'/'
        BEQ EXPR_MUL_DIV
        RTS
EXPR_MUL_TIMES:
        JSR STRNONE
        JSR ADV
        JSR PUSHNUM
        JSR EXPR_POW
        JSR FAC2ARG
        JSR POPNUM
        JSR FMUL
        JMP EXPR_MUL_LOOP
EXPR_MUL_DIV:
        JSR STRNONE
        JSR ADV
        JSR PUSHNUM
        JSR EXPR_POW
        JSR FAC2ARG
        JSR POPNUM
        JSR FDIV
        BCS ERR_DIVISION_BY_ZERO
        JMP EXPR_MUL_LOOP

; A power binds tighter than a multiply and looser than a minus, so -2^2 is -4
; and the right operand is taken at unary level.
EXPR_POW:
        JSR EXPR_UNARY
EXPR_POW_LOOP:
        JSR SKIPSP
        JSR CURCHAR
        CMP #'^'
        BNE EXPR_POW_DONE
        JSR STRNONE
        JSR ADV
        JSR PUSHNUM
        JSR EXPR_UNARY
        JSR FAC2ARG
        JSR POPNUM
        JSR FPOW
        JMP EXPR_POW_LOOP
EXPR_POW_DONE:
        RTS

EXPR_UNARY:
        JSR SKIPSP
        JSR CURCHAR
        CMP #'-'
        BNE EXPR_UNARY_PLUS
        JSR ADV
        JSR EXPR_UNARY
        JSR FNEG
        RTS
EXPR_UNARY_PLUS:
        CMP #'+'
        BNE EXPR_OPERAND
        JSR ADV
        JSR EXPR_UNARY
        RTS

; ---------------------------------------------------------------------------
; Operands
; ---------------------------------------------------------------------------

EXPR_OPERAND:
        JSR SKIPSP
        JSR CURCHAR
        CMP #'('
        BNE EXPR_OP_NOTPAREN
        JSR ADV
        JSR EXPR
        JSR SKIPSP
        JSR CURCHAR
        CMP #')'
        BNE ERR_SYNTAX
        JSR ADV
        RTS
EXPR_OP_NOTPAREN:
        CMP #'"'
        BEQ EXPR_OP_STRING
        CMP #'0'
        BCC EXPR_OP_VARIABLE
        CMP #'9'+1
        BCS EXPR_OP_VARIABLE
        JSR FVAL
        BCS ERR_SYNTAX
        JMP STRCLR              ; a number is not a string, whatever came before
EXPR_OP_STRING:
        JSR ADV                 ; past the opening quote
        LDA B_IPTR
        STA B_STRPTR
        LDA B_IPTR+1
        STA B_STRPTR+1
        LDA #0
        STA B_STRLEN
EXPR_OP_STRING_LOOP:
        JSR CURCHAR
        BEQ EXPR_OP_STRING_END  ; an unterminated string ends the line
        CMP #'"'
        BEQ EXPR_OP_STRING_SHUT
        INC B_STRLEN
        JSR ADV
        JMP EXPR_OP_STRING_LOOP
EXPR_OP_STRING_SHUT:
        JSR ADV
EXPR_OP_STRING_END:
        JMP STRSET

; A variable, or a one-letter function call. The name is read twice: once to
; see what follows it, once for the parser that consumes it.
EXPR_OP_VARIABLE:
        JSR FNCALL
        BCS EXPR_OP_VDONE
        LDA B_IPTR
        STA B_TMPP2
        LDA B_IPTR+1
        STA B_TMPP2+1
        JSR PARSENAME
        BCS ERR_SYNTAX
        JSR CURCHAR
        CMP #'('
        BNE EXPR_OP_PLAINVAR
        JMP ARY_GET
EXPR_OP_PLAINVAR:
        LDA B_TMPP2
        STA B_IPTR
        LDA B_TMPP2+1
        STA B_IPTR+1
        JSR PARSENAME
        JSR VARREF
EXPR_OP_VDONE:
        RTS

; ---------------------------------------------------------------------------
; Variables: a name of one or two characters, and a type.
; ---------------------------------------------------------------------------

VARSIZE = 8             ; name, name, type, five bytes of value

; PARSENAME: reads a name at (B_IPTR) into B_TEMP/B_TEMP2 and its type into
; B_TEMP3. Carry set when there is no name there.
PARSENAME:
        JSR SKIPSP
        JSR CURCHAR
        JSR UPPER
        CMP #'A'
        BCC PARSENAME_NONE
        CMP #'Z'+1
        BCS PARSENAME_NONE
        STA B_TEMP
        JSR ADV
        LDA #0
        STA B_TEMP2
        JSR CURCHAR
        JSR UPPER
        CMP #'A'
        BCC PARSENAME_TYPE
        CMP #'Z'+1
        BCS PARSENAME_DIGIT
        STA B_TEMP2
        JSR ADV
        JMP PARSENAME_TYPE
PARSENAME_DIGIT:
        CMP #'0'
        BCC PARSENAME_TYPE
        CMP #'9'+1
        BCS PARSENAME_TYPE
        STA B_TEMP2
        JSR ADV
PARSENAME_TYPE:
        LDA #T_NUM
        STA B_TEMP3
        JSR CURCHAR
        CMP #'$'
        BNE PARSENAME_OK
        LDA #T_STR
        STA B_TEMP3
        JSR ADV
PARSENAME_OK:
        CLC
        RTS
PARSENAME_NONE:
        SEC
        RTS

; VARFIND: name in B_TEMP/B_TEMP2/B_TEMP3. -> B_TMPP = entry, carry clear; or
; carry set when the name is not there.
VARFIND:
        LDA B_VARTAB
        STA B_TMPP
        LDA B_VARTAB+1
        STA B_TMPP+1
VARFIND_LOOP:
        LDA B_TMPP
        CMP B_ARYTAB
        LDA B_TMPP+1
        SBC B_ARYTAB+1
        BCS VARFIND_MISS
        LDY #0
        LDA (B_TMPP),Y
        CMP B_TEMP
        BNE VARFIND_NEXT
        INY
        LDA (B_TMPP),Y
        CMP B_TEMP2
        BNE VARFIND_NEXT
        INY
        LDA (B_TMPP),Y
        CMP B_TEMP3
        BNE VARFIND_NEXT
        CLC
        RTS
VARFIND_NEXT:
        CLC
        LDA B_TMPP
        ADC #VARSIZE
        STA B_TMPP
        BCC VARFIND_LOOP
        INC B_TMPP+1
        JMP VARFIND_LOOP
VARFIND_MISS:
        SEC
        RTS

; VARREF: the value of the variable named in B_TEMP.. becomes FAC.
VARREF:
        JSR VARFIND
        BCS VARREF_NEW
        LDA B_TEMP3
        CMP #T_STR
        BEQ VARREF_STRING
        JSR STRCLR
        LDY #3
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
VARREF_STRING:
        LDY #3
        LDA (B_TMPP),Y
        STA B_STRLEN
        INY
        LDA (B_TMPP),Y
        STA B_STRPTR
        INY
        LDA (B_TMPP),Y
        STA B_STRPTR+1
        JMP STRSET
VARREF_NEW:
        LDA B_TEMP3
        CMP #T_STR
        BEQ VARREF_NEW_STRING
        JSR VARCREATE
        JMP FZERO
VARREF_NEW_STRING:
        JSR VARCREATE
        LDA #0
        STA B_STRLEN
        LDA #<B_STRCHR
        STA B_STRPTR
        LDA #>B_STRCHR
        STA B_STRPTR+1
        JMP STRSET

; VARSET: FAC becomes the value of the variable named in B_TEMP...
VARSET:
        LDA B_TEMP3
        CMP #T_STR
        BEQ ERR_STRING_NOT_YET
        JSR VARFIND
        BCC VARSET_HAVE
        JSR VARCREATE
VARSET_HAVE:
        LDY #3
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

; VARCREATE: a new variable, zeroed. -> B_TMPP = it.
VARCREATE:
        LDA B_ARYTAB
        STA B_TMPP
        LDA B_ARYTAB+1
        STA B_TMPP+1
        CLC
        LDA B_ARYTAB
        ADC #VARSIZE
        STA B_ARYTAB
        LDA B_ARYTAB+1
        ADC #0
        STA B_ARYTAB+1
        LDA B_ARYTAB
        CMP B_FRETOP
        LDA B_ARYTAB+1
        SBC B_FRETOP+1
        BCS ERR_OUT_OF_MEMORY
        LDY #0
        LDA B_TEMP
        STA (B_TMPP),Y
        INY
        LDA B_TEMP2
        STA (B_TMPP),Y
        INY
        LDA B_TEMP3
        STA (B_TMPP),Y
        LDX #5
        INY
VARCREATE_ZERO:
        LDA #0
        STA (B_TMPP),Y
        INY
        DEX
        BNE VARCREATE_ZERO
        RTS
