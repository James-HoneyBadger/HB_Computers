; SPDX-License-Identifier: MIT
;
; str.s -- string variables. A string lives in a heap that grows down from the
; top of memory; a variable holds its length and the address of its bytes. There
; is no collector: what is written is not given back, which is what a machine of
; this size could afford.

; ---------------------------------------------------------------------------
; The last operand
; ---------------------------------------------------------------------------
; B_STRPTR/B_STRLEN hold a string operand and bit 1 of B_FLAGS says it is one,
; exactly as FAC holds a number.

STRSET:
        LDA B_FLAGS
        ORA #2
        STA B_FLAGS
        RTS

STRCLR:
        LDA B_FLAGS
        AND #$FD
        STA B_FLAGS
        RTS

; STRWANT: the last operand must have been a string.
STRWANT:
        LDA B_FLAGS
        AND #2
        BNE STRWANT_OK
        JMP ERR_TYPE_MISMATCH
STRWANT_OK:
        RTS

; STRNONE: the last operand must have been a number.
STRNONE:
        LDA B_FLAGS
        AND #2
        BEQ STRNONE_OK
        JMP ERR_TYPE_MISMATCH
STRNONE_OK:
        RTS

; ---------------------------------------------------------------------------
; The heap
; ---------------------------------------------------------------------------

; STRALLOC: reserve B_TMP bytes. On return B_STREND is their address; carry set
; when there is no room left above the program and its variables.
STRALLOC:
        SEC
        LDA B_STREND
        SBC B_TMP
        STA B_TEMPSTR2
        LDA B_STREND+1
        SBC B_TMP+1             ; the size is a word: an array bigger than 255
        STA B_TEMPSTR2+1        ; bytes has to move the heap by its high byte too
        LDA B_TEMPSTR2
        CMP B_ARYTAB
        LDA B_TEMPSTR2+1
        SBC B_ARYTAB+1
        BCC STRALLOC_FULL
        LDA B_TEMPSTR2
        STA B_STREND
        LDA B_TEMPSTR2+1
        STA B_STREND+1
        CLC
        RTS
STRALLOC_FULL:
        SEC
        RTS

; ---------------------------------------------------------------------------
; Assigning
; ---------------------------------------------------------------------------

; STRASSIGN: the variable named in B_TEMP.. takes a copy of the string at
; B_STRPTR, B_STRLEN bytes of it.
STRASSIGN:
        JSR STRWANT
        JSR VARFIND
        BCC STRASSIGN_HAVE
        JSR VARCREATE
STRASSIGN_HAVE:
        LDA B_STRLEN
        BEQ STRASSIGN_EMPTY
        STA B_TMP
        JSR STRALLOC
        BCS ERR_OUT_OF_MEMORY
        LDY #0
STRASSIGN_COPY:
        CPY B_STRLEN
        BEQ STRASSIGN_STORE
        LDA (B_STRPTR),Y
        STA (B_STREND),Y
        INY
        JMP STRASSIGN_COPY
STRASSIGN_STORE:
        LDY #3
        LDA B_STRLEN
        STA (B_TMPP),Y
        INY
        LDA B_STREND
        STA (B_TMPP),Y
        INY
        LDA B_STREND+1
        STA (B_TMPP),Y
        RTS
STRASSIGN_EMPTY:
        LDY #3
        LDA #0
        STA (B_TMPP),Y
        INY
        STA (B_TMPP),Y
        INY
        STA (B_TMPP),Y
        RTS

; ---------------------------------------------------------------------------
; Printing
; ---------------------------------------------------------------------------

; STRPRINT: the string at B_STRPTR, B_STRLEN characters of it, goes to the tube.
STRPRINT:
        LDA #0
        STA B_TEMP
STRPRINT_LOOP:
        LDY B_TEMP
        CPY B_STRLEN
        BEQ STRPRINT_DONE
        LDA (B_STRPTR),Y
        JSR K_CHROUT
        INC B_TEMP
        JMP STRPRINT_LOOP
STRPRINT_DONE:
        RTS

; ---------------------------------------------------------------------------
; Reading the arguments of the string functions
; ---------------------------------------------------------------------------

STRARG_OPEN:
        JSR SKIPSP
        JSR CURCHAR
        CMP #'('
        BNE ERR_SYNTAX
        JMP ADV

STRARG_COMMA:
        JSR SKIPSP
        JSR CURCHAR
        CMP #','
        BNE ERR_SYNTAX
        JMP ADV

STRARG_CLOSE:
        JSR SKIPSP
        JSR CURCHAR
        CMP #')'
        BNE ERR_SYNTAX
        JMP ADV

; STRKEEP: the string in hand is set aside, because the count that follows may
; take another string apart.
STRKEEP:
        LDA B_STRPTR
        STA B_TEMPSTR
        LDA B_STRPTR+1
        STA B_TEMPSTR+1
        LDA B_STRLEN
        STA B_STRLEN3
        RTS

STRKEPT:
        LDA B_TEMPSTR
        STA B_STRPTR
        LDA B_TEMPSTR+1
        STA B_STRPTR+1
        LDA B_STRLEN3
        STA B_STRLEN
        RTS

; FN_ONESTR: the argument of a function that takes a string is one string
; expression in brackets.
FN_ONESTR:
        JSR STRARG_OPEN
        JSR EXPR
        JSR STRWANT
        JMP STRARG_CLOSE

; ---------------------------------------------------------------------------
; The string functions
; ---------------------------------------------------------------------------

FN_LEN:
        JSR FN_ONESTR
        JSR STRCLR
        LDA B_STRLEN
        LDY #0
        JMP ITOF

FN_ASC:
        JSR FN_ONESTR
        JSR STRCLR
        LDA B_STRLEN
        BEQ FN_ASC_EMPTY
        LDY #0
        LDA (B_STRPTR),Y
        LDY #0
        JMP ITOF
FN_ASC_EMPTY:
        JMP ERR_ILLEGAL_QUANTITY

FN_CHR:
        JSR FN_ONEPAREN
        JSR FTOI
        BCS ERR_ILLEGAL_QUANTITY
        STA B_STRLEN3           ; one character, in a scratch of its own
        LDA #<B_STRCHR
        STA B_STRPTR
        LDA #>B_STRCHR
        STA B_STRPTR+1
        LDY #0
        LDA B_STRLEN3
        STA (B_STRPTR),Y
        LDA #1
        STA B_STRLEN
        JMP STRSET

FN_STR:
        JSR FN_ONEPAREN
        JSR FPRINT
        LDA #<B_NUMBUF
        STA B_STRPTR
        LDA #>B_NUMBUF
        STA B_STRPTR+1
        LDY #0
FN_STR_LEN:
        LDA (B_STRPTR),Y
        BEQ FN_STR_DONE
        INY
        BNE FN_STR_LEN
FN_STR_DONE:
        STY B_STRLEN
        JMP STRSET

; VAL reads the number at the front of the string. The parser wants a terminated
; string, so a copy goes into the heap and is handed back as soon as it is read.
FN_VAL:
        JSR FN_ONESTR
        JSR STRCLR
        LDA B_STRLEN
        CLC
        ADC #1
        BEQ FN_VAL_FAIL
        STA B_TMP
        JSR STRALLOC
        BCS ERR_OUT_OF_MEMORY
        LDA B_TMP
        STA B_STRLEN2           ; the size, to be given back
        LDY #0
FN_VAL_COPY:
        CPY B_STRLEN
        BEQ FN_VAL_END
        LDA (B_STRPTR),Y
        STA (B_STREND),Y
        INY
        JMP FN_VAL_COPY
FN_VAL_END:
        LDA #0
        STA (B_STREND),Y
        LDA B_IPTR
        PHA
        LDA B_IPTR+1
        PHA
        LDA B_STREND
        STA B_IPTR
        LDA B_STREND+1
        STA B_IPTR+1
        JSR FVAL
        LDA #0
        ROL A                   ; keep the carry, which says whether it read
        STA B_ERRFLAG
        PLA
        STA B_IPTR+1
        PLA
        STA B_IPTR
        ; the copy has been read, so the heap gets its room back
        LDA B_STREND
        CLC
        ADC B_STRLEN2
        STA B_STREND
        BCC FN_VAL_GIVEN
        INC B_STREND+1
FN_VAL_GIVEN:
        LDA B_ERRFLAG
        BNE FN_VAL_BAD
        JMP STRCLR
FN_VAL_BAD:
        JMP ERR_SYNTAX
FN_VAL_FAIL:
        JMP ERR_ILLEGAL_QUANTITY

FN_LEFT:
        JSR STRARG_OPEN
        JSR EXPR
        JSR STRWANT
        JSR STRKEEP
        JSR STRARG_COMMA
        JSR EXPR
        JSR FTOI
        BCS ERR_ILLEGAL_QUANTITY
        STA B_INT2
        STY B_INT2+1
        JSR STRARG_CLOSE
        JSR STRKEPT             ; the address comes back; the length is new
        LDA B_INT2+1
        BNE FN_LEFT_WHOLE       ; far more than the string holds
        LDA B_INT2
        CMP B_STRLEN3
        BCC FN_LEFT_SET
FN_LEFT_WHOLE:
        LDA B_STRLEN3
FN_LEFT_SET:
        STA B_STRLEN
        JMP STRSET

FN_RIGHT:
        JSR STRARG_OPEN
        JSR EXPR
        JSR STRWANT
        JSR STRKEEP
        JSR STRARG_COMMA
        JSR EXPR
        JSR FTOI
        BCS ERR_ILLEGAL_QUANTITY
        STA B_INT2
        STY B_INT2+1
        JSR STRARG_CLOSE
        JSR STRKEPT
        LDA B_INT2+1
        BNE FN_RIGHT_WHOLE
        LDA B_INT2
        CMP B_STRLEN3
        BCC FN_RIGHT_SET
FN_RIGHT_WHOLE:
        LDA B_STRLEN3
FN_RIGHT_SET:
        STA B_STRLEN
        LDA B_STRLEN3
        SEC
        SBC B_STRLEN
        CLC
        ADC B_TEMPSTR
        STA B_STRPTR
        LDA B_TEMPSTR+1
        ADC #0
        STA B_STRPTR+1
        JMP STRSET

FN_MID:
        JSR STRARG_OPEN
        JSR EXPR
        JSR STRWANT
        JSR STRKEEP
        JSR STRARG_COMMA
        JSR EXPR                ; the first character wanted, counting from one
        JSR FTOI
        BCS ERR_ILLEGAL_QUANTITY
        STA B_INT2
        JSR SKIPSP
        JSR CURCHAR
        CMP #','
        BNE FN_MID_TO_END
        JSR ADV
        JSR EXPR                ; how many, when it is given
        JSR FTOI
        BCS ERR_ILLEGAL_QUANTITY
        STA B_INT2+1
        JMP FN_MID_CLOSE
FN_MID_TO_END:
        LDA #$FF
        STA B_INT2+1
FN_MID_CLOSE:
        JSR STRARG_CLOSE
        LDA B_INT2
        SEC
        SBC #1
        STA B_INT2
        CMP B_STRLEN3
        BCC FN_MID_INSIDE
        LDA #0                  ; starting past the end leaves nothing
        STA B_STRLEN
        JSR STRKEPT
        JMP STRSET
FN_MID_INSIDE:
        LDA B_STRLEN3
        SEC
        SBC B_INT2
        STA B_STRLEN            ; what is left from there
        LDA B_INT2+1
        CMP #$FF
        BEQ FN_MID_NOLIMIT
        CMP B_STRLEN
        BCS FN_MID_NOLIMIT
        STA B_STRLEN
FN_MID_NOLIMIT:
        LDA B_TEMPSTR
        CLC
        ADC B_INT2
        STA B_STRPTR
        LDA B_TEMPSTR+1
        ADC #0
        STA B_STRPTR+1
        JMP STRSET
