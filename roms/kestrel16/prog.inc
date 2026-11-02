; SPDX-License-Identifier: MIT
;
; prog.inc -- the stored program, and the control flow that needs one.
;
; A program is a run of records in bank zero, from PROGSTART up to PROG_LIMIT:
;
;       number (two bytes, low first), the text as typed, and a zero
;
; There are no link pointers. A line is found by scanning, which is slower than
; following a pointer and cannot be left pointing into the middle of a line that
; has been edited: inserting and deleting are byte moves, and every line after
; them stays where it is. Thirty kilobytes of lines on an eight-megahertz
; processor is not a scan anybody waits for.
;
; The two stacks live above the program, where nothing else wants to be: FOR
; frames at FOR_BASE, GOSUB frames at GOSUB_BASE. A frame holds the address of
; the text it comes back to rather than a line number, so an edit elsewhere in
; the program cannot misdirect it.
;
; Every routine here works in sixteen-bit A and leaves it that way. The caller is
; in eight-bit mode between statements, so it says REP #$20 before it tests a
; result and SEP #$20 after. The same rule the rest of the ROM follows, and the
; rule that keeps a wrong-width immediate from becoming an instruction nobody
; wrote.

; ---------------------------------------------------------------------------
; The program
; ---------------------------------------------------------------------------

; PROG_NEW: no program, no variables, no loops.
PROG_NEW:
        PHP
        JSR CLEAR_VARS
        JSR RESET_STACKS
        REP #$20
        LDA.W #PROGSTART
        STA ZP_PROGTOP
        LDA.W #0
        STA PROGSTART           ; the end marker: a zero number
        SEP #$20
        PLP
        RTS

; CLEAR_VARS: every variable is zero, as after NEW or RUN. A string variable's
; zero is a length of nought: its text is still there, but nothing reads past
; the length, so the slot behaves as empty.
CLEAR_VARS:
        PHP
        REP #$20
        LDA.W #0
        LDX.W #0
CLEAR_VARS_LOOP:
        STA VARS,X
        INX
        INX
        CPX.W #52
        BCC CLEAR_VARS_LOOP
        SEP #$20
        JSR CLEAR_STRS
        JSR CLEAR_ARRAYS
        PLP
        RTS

; CLEAR_STRS: the length byte of every string slot goes to nought.
CLEAR_STRS:
        PHP
        REP #$10
        REP #$20
        LDX.W #0
CLEAR_STRS_LOOP:
        LDA.W #0
        STA.L STRINGS,X
        TXA
        CLC
        ADC.W #STR_SLOT
        TAX
        CPX.W #(26*STR_SLOT)
        BCC CLEAR_STRS_LOOP
        SEP #$20
        PLP
        RTS

; CLEAR_ARRAYS: every letter's array and every letter's definition is forgotten
; and the heap starts again at the bottom. The runs themselves are not wiped: a
; count of nought is what says an array is not there, and the next DIM writes
; over what it takes.
CLEAR_ARRAYS:
        PHP
        REP #$10
        REP #$20
        LDX.W #0
CLEAR_ARRAYS_LOOP:
        LDA.W #0
        STA.L ARRAYS,X          ; the run's offset, both bytes of it
        STA.L ARRAYS+2,X        ; its count of elements, and
        STA.L ARRAYS+4,X        ; the rows and columns it was given:
        STA.L ARRAYS+6,X        ; eight bytes an entry, not four
        TXA
        CLC
        ADC.W #8
        TAX
        CPX.W #(26*8)
        BCC CLEAR_ARRAYS_LOOP
        LDA.W #ARR_HEAP
        STA.L ARR_TOP
        ; the definitions go with them: a bank of nought is a letter with none
        LDX.W #0
CLEAR_ARRAYS_DEFS:
        LDA.W #0
        STA.L DEFNS,X
        STA.L DEFNS+2,X
        TXA
        CLC
        ADC.W #4
        TAX
        CPX.W #(26*4)
        BCC CLEAR_ARRAYS_DEFS
        SEP #$20
        PLP
        RTS

; RESET_STACKS: no loops and no subroutines pending.
RESET_STACKS:
        PHP
        SEP #$20
        LDA #0
        STA ZP_FORSP
        STA ZP_GSUBSP
        STA ZP_JFLAG
        PLP
        RTS

; ---------------------------------------------------------------------------
; Reading records
; ---------------------------------------------------------------------------

; PP_SET_QP: ZP_QP = ZP_PP. Both are bank-zero pointers into the program.
PP_SET_QP:
        REP #$20
        LDA ZP_PP
        STA ZP_QP
        SEP #$20
        LDA #BANK_KERNAL
        STA ZP_QP+2
        RTS

; PP_NUMBER: A = the number of the record at ZP_PP. Its scratch is ZP_CNT2 and
; not ZP_TMP16, because a caller looking for a line keeps the number it wants in
; ZP_TMP16 while it scans -- and used to find every line identical to itself.
PP_NUMBER:
        SEP #$20                ; the record is bytes, whatever the caller's mode
        LDY.W #0
        LDA [ZP_PP],Y
        STA ZP_CNT2
        INY
        LDA [ZP_PP],Y
        STA ZP_CNT2+1
        REP #$20
        LDA ZP_CNT2
        RTS

; PP_SIZE: A = the length of the record at ZP_PP: number, text, terminator.
PP_SIZE:
        SEP #$20                ; the text is bytes
        LDY.W #2
PP_SIZE_LOOP:
        LDA [ZP_PP],Y
        BEQ PP_SIZE_DONE
        INY
        BRA PP_SIZE_LOOP
PP_SIZE_DONE:
        REP #$20
        TYA
        INC A
        RTS

; FIND_POS: ZP_PP = the first record numbered at least ZP_TMP16, or the end
; marker when the number is past every line.
FIND_POS:
        PHP
        REP #$20
        LDA.W #PROGSTART
        STA ZP_PP
        SEP #$20
        LDA #BANK_KERNAL
        STA ZP_PP+2
FIND_POS_LOOP:
        JSR PP_NUMBER
        CMP.W #0
        BEQ FIND_POS_DONE       ; the end of the program
        CMP ZP_TMP16
        BCS FIND_POS_DONE       ; this line, or the first past it
        JSR PP_SIZE
        CLC
        ADC ZP_PP
        STA ZP_PP
        BRA FIND_POS_LOOP
FIND_POS_DONE:
        SEP #$20
        PLP
        RTS

; FIND_LINE: ZP_TMP16 = a line number. Carry set when the line exists, with
; ZP_LINE pointing at it.
FIND_LINE:
        JSR FIND_POS
        JSR PP_NUMBER
        CMP ZP_TMP16
        BNE FIND_LINE_MISSING
        REP #$20
        LDA ZP_PP
        STA ZP_LINE
        SEP #$20
        SEC
        RTS
FIND_LINE_MISSING:
        SEP #$20
        CLC
        RTS

; COPY_ASC: ZP_CNT bytes from ZP_QP to ZP_PP, ascending. For moving a block down.
COPY_ASC:
        REP #$20
        LDA ZP_CNT
        BEQ COPY_ASC_DONE
        SEP #$20
        LDY.W #0
COPY_ASC_LOOP:
        LDA [ZP_QP],Y
        STA [ZP_PP],Y
        INY
        CPY ZP_CNT
        BCC COPY_ASC_LOOP
COPY_ASC_DONE:
        SEP #$20
        RTS

; COPY_DESC: the same bytes, from the top down. For moving a block up, where
; ascending would overwrite the byte that has not been read yet.
COPY_DESC:
        REP #$20
        LDA ZP_CNT
        BEQ COPY_DESC_DONE
        TAY
        DEY
        SEP #$20
COPY_DESC_LOOP:
        LDA [ZP_QP],Y
        STA [ZP_PP],Y
        DEY
        BPL COPY_DESC_LOOP
COPY_DESC_DONE:
        SEP #$20
        RTS

; ---------------------------------------------------------------------------
; Storing a line
; ---------------------------------------------------------------------------

; STORE_LINE: the number is in ZP_TMP16, the text at ZP_SRC. A line with no text
; deletes the numbered line, which is how a line is removed.
STORE_LINE:
        JSR FIND_POS
        JSR PP_NUMBER
        REP #$20
        CMP ZP_TMP16
        SEP #$20
        BNE STORE_INSERT
        JSR DELETE_AT           ; retyping a line replaces it
STORE_INSERT:
        JSR SKIPSP
        LDA [ZP_SRC]
        BEQ STORE_DONE
        JSR COUNT_TEXT
        REP #$20
        LDA ZP_TMPLEN
        CLC
        ADC.W #3                ; number, text, terminator
        STA ZP_NEWLEN
        LDA ZP_PROGTOP
        CLC
        ADC ZP_NEWLEN
        CMP.W #PROG_LIMIT
        SEP #$20
        BCS STORE_FULL
        JSR MAKE_ROOM
        JSR WRITE_RECORD
STORE_DONE:
        SEP #$20
        RTS
STORE_FULL:
        JSR SAY_NOROOM
        RTS

; COUNT_TEXT: the zero-terminated text at ZP_SRC; its length in ZP_TMPLEN.
COUNT_TEXT:
        REP #$20
        LDA ZP_SRC
        STA ZP_QP
        SEP #$20
        LDA #BANK_KERNAL
        STA ZP_QP+2
        REP #$20
        LDA.W #0
        STA ZP_TMPLEN
        SEP #$20
        LDY.W #0
COUNT_TEXT_LOOP:
        LDA [ZP_QP],Y
        BEQ COUNT_TEXT_DONE
        REP #$20
        INC ZP_TMPLEN
        SEP #$20
        INY
        BRA COUNT_TEXT_LOOP
COUNT_TEXT_DONE:
        RTS

; MAKE_ROOM: open ZP_NEWLEN bytes at ZP_PP and move the program's tail up into
; them. The tail moves from its top down.
MAKE_ROOM:
        REP #$20
        LDA ZP_PROGTOP
        SEC
        SBC ZP_PP
        BEQ MAKE_ROOM_GROW      ; nothing above: nothing to move
        STA ZP_CNT
        ; the source is ZP_PP, the destination ZP_PP + ZP_NEWLEN
        LDA ZP_PP
        STA ZP_QP
        CLC
        ADC ZP_NEWLEN
        STA ZP_PP
        SEP #$20
        LDA #BANK_KERNAL
        STA ZP_QP+2
        STA ZP_PP+2
        JSR COPY_DESC
        ; and back to where the new record goes
        REP #$20
        LDA ZP_PP
        SEC
        SBC ZP_NEWLEN
        STA ZP_PP
MAKE_ROOM_GROW:
        REP #$20
        LDA ZP_PROGTOP
        CLC
        ADC ZP_NEWLEN
        STA ZP_PROGTOP
        SEP #$20
        JSR END_MARK
        RTS

; DELETE_AT: remove the record at ZP_PP and close the gap. Its scratches are
; ZP_CNT2 and ZP_NEWLEN: ZP_TMP16 belongs to the caller, which is holding the
; number it is about to store in the space this makes.
DELETE_AT:
        JSR PP_SIZE             ; A = the record's length
        STA ZP_CNT2
        ; what is left above it: PROGTOP - (ZP_PP + size)
        LDA ZP_PROGTOP
        SEC
        SBC ZP_PP
        SEC
        SBC ZP_CNT2
        BEQ DELETE_AT_SHRINK
        STA ZP_NEWLEN           ; the move's length
        LDA ZP_PP
        CLC
        ADC ZP_CNT2
        STA ZP_QP               ; source: the record above
        SEP #$20
        LDA #BANK_KERNAL
        STA ZP_QP+2
        REP #$20
        LDA ZP_NEWLEN
        STA ZP_CNT
        JSR COPY_ASC
DELETE_AT_SHRINK:
        REP #$20
        LDA ZP_PROGTOP
        SEC
        SBC ZP_CNT2
        STA ZP_PROGTOP
        SEP #$20
        JSR END_MARK
        RTS

; END_MARK: put the zero word that ends the program at PROGTOP. The scan stops
; at a zero number, so this has to be written after every move rather than left
; to whatever the last line happened to end with.
END_MARK:
        REP #$20
        LDA ZP_PROGTOP
        STA ZP_QP
        SEP #$20
        LDA #BANK_KERNAL
        STA ZP_QP+2
        REP #$20
        LDA.W #0
        STA [ZP_QP]
        SEP #$20
        RTS

; WRITE_RECORD: the number and the text go at ZP_PP, which MAKE_ROOM has made
; room for; the terminator follows the text.
WRITE_RECORD:
        REP #$20
        LDA ZP_TMP16
        STA ZP_LNUM             ; the number, kept while the copy runs
        SEP #$20
        LDY.W #0
        LDA ZP_LNUM
        STA [ZP_PP],Y
        INY
        LDA ZP_LNUM+1
        STA [ZP_PP],Y
        ; the text, from the line buffer, terminator and all
        JSR PP_SET_QP           ; ZP_QP = the record
        REP #$20
        LDA ZP_QP
        CLC
        ADC.W #2
        STA ZP_PP               ; ZP_PP = where the text goes
        SEP #$20
        LDA #BANK_KERNAL
        STA ZP_PP+2
        REP #$20
        LDA ZP_SRC
        STA ZP_QP
        SEP #$20
        LDA #BANK_KERNAL
        STA ZP_QP+2
        LDY.W #0
WRITE_RECORD_LOOP:
        LDA [ZP_QP],Y
        STA [ZP_PP],Y
        BEQ WRITE_RECORD_DONE
        INY
        BRA WRITE_RECORD_LOOP
WRITE_RECORD_DONE:
        RTS

; ---------------------------------------------------------------------------
; LIST
; ---------------------------------------------------------------------------

; DO_LIST: `LIST`, or `LIST 100` from that line on.
DO_LIST:
        JSR SKIPSP
        LDA [ZP_SRC]
        BEQ DO_LIST_ALL
        JSR EXPR
        REP #$20
        STX ZP_TMP16
        SEP #$20
        JSR FIND_POS
        BRA DO_LIST_WALK
DO_LIST_ALL:
        REP #$20
        LDA.W #PROGSTART
        STA ZP_PP
        SEP #$20
        LDA #BANK_KERNAL
        STA ZP_PP+2
DO_LIST_WALK:
        JSR PP_NUMBER
        CMP.W #0
        BEQ DO_LIST_DONE
        TAX                     ; the number, for PRNUM
        PHX
        SEP #$20
        JSR PP_SET_QP
        PLX
        JSR PRNUM
        LDA #32
        JSL L_CHROUT
        LDA #32
        JSL L_CHROUT
        LDY.W #2
DO_LIST_TEXT:
        LDA [ZP_QP],Y
        BEQ DO_LIST_NEXT
        JSL L_CHROUT
        INY
        BRA DO_LIST_TEXT
DO_LIST_NEXT:
        JSR SAY_NEWLINE
        JSR PP_SIZE
        CLC
        ADC ZP_PP
        STA ZP_PP
        BRA DO_LIST_WALK
DO_LIST_DONE:
        SEP #$20
        RTS


; ---------------------------------------------------------------------------
; RUN, and GOTO
; ---------------------------------------------------------------------------

; DO_RUN: `RUN`, or `RUN 100` to start there. Variables are cleared, as they are
; on every machine this one remembers.
DO_RUN:
        JSR SKIPSP
        LDA [ZP_SRC]
        BEQ DO_RUN_START
        JSR EXPR
        REP #$20
        STX ZP_TMP16
        SEP #$20
        JSR CLEAR_VARS
        JSR RESET_STACKS
        JSR FIND_LINE
        BCC DO_RUN_MISSING
        LDA #1
        STA ZP_JFLAG
        RTS
DO_RUN_START:
        JSR CLEAR_VARS
        JSR RESET_STACKS
        REP #$20
        LDA.W #PROGSTART
        STA ZP_LINE
        SEP #$20
        LDA #1
        STA ZP_JFLAG
        RTS
DO_RUN_MISSING:
        JSR SAY_NOLINE
        LDA #3
        STA ZP_JFLAG
        RTS

DO_GOTO:
        JSR EXPR
        REP #$20
        STX ZP_TMP16
        SEP #$20
        JSR FIND_LINE
        BCC DO_GOTO_MISSING
        LDA #1
        STA ZP_JFLAG
        RTS
DO_GOTO_MISSING:
        JSR SAY_NOLINE
        LDA #3
        STA ZP_JFLAG
        RTS

; ---------------------------------------------------------------------------
; Where a line's text is
; ---------------------------------------------------------------------------

; TEXT_BASE: A = the address of the text being run -- a program line's, or the
; prompt's own line. ZP_LINE is $FFFF for the prompt.
TEXT_BASE:
        REP #$20
        LDA ZP_LINE
        CMP.W #$FFFF
        BEQ TEXT_BASE_DIRECT
        CLC
        ADC.W #2
        RTS
TEXT_BASE_DIRECT:
        LDA ZP_DLINE
        RTS

; TEXT_OFFSET: ZP_BASE = the text's address, ZP_OFF = how far into it the next
; statement begins.
TEXT_OFFSET:
        JSR TEXT_BASE
        STA ZP_BASE
        REP #$20
        LDA ZP_SRC
        SEC
        SBC ZP_BASE
        STA ZP_OFF
        SEP #$20
        RTS

; STORE_VAR: X becomes the value of the variable numbered ZP_VAR.
STORE_VAR:
        REP #$20
        LDA ZP_VAR
        AND.W #$00FF
        ASL A
        TAY
        TXA
        STA VARS,Y
        SEP #$20
        RTS

; LOAD_VAR: X = the value of the variable numbered in A.
LOAD_VAR:
        REP #$20
        AND.W #$00FF
        ASL A
        TAX
        LDA VARS,X
        TAX
        SEP #$20
        RTS

; ---------------------------------------------------------------------------
; GOSUB and RETURN
; ---------------------------------------------------------------------------

DO_GOSUB:
        JSR EXPR
        REP #$20
        STX ZP_TMP16
        SEP #$20
        LDA ZP_GSUBSP
        CMP #GOSUB_MAX
        BCS DO_GOSUB_FULL
        JSR TEXT_OFFSET
        JSR PUSH_GOSUB
        JSR FIND_LINE
        BCC DO_GOSUB_MISSING
        LDA #1
        STA ZP_JFLAG
        RTS
DO_GOSUB_FULL:
        JSR SAY_NOROOM
        LDA #3
        STA ZP_JFLAG
        RTS
DO_GOSUB_MISSING:
        JSR SAY_NOLINE
        LDA #3
        STA ZP_JFLAG
        RTS

; PUSH_GOSUB: ZP_BASE and ZP_OFF are the line and the place in it to come back to.
PUSH_GOSUB:
        REP #$20
        LDA ZP_GSUBSP
        AND.W #$00FF
        ASL A
        ASL A
        TAX
        LDA ZP_BASE
        STA GOSUB_BASE,X
        LDA ZP_OFF
        STA GOSUB_BASE+2,X
        SEP #$20
        INC ZP_GSUBSP
        RTS

DO_RETURN:
        LDA ZP_GSUBSP
        BNE DO_RETURN_POP
        JSR SAY_RETGOSUB
        LDA #3
        STA ZP_JFLAG
        RTS
DO_RETURN_POP:
        DEC A
        STA ZP_GSUBSP
        REP #$20
        AND.W #$00FF
        ASL A
        ASL A
        TAX
        LDA GOSUB_BASE,X
        STA ZP_BASE
        LDA GOSUB_BASE+2,X
        CLC
        ADC ZP_BASE
        STA ZP_SRC
        SEP #$20
        LDA #BANK_KERNAL
        STA ZP_SRC+2
        LDA #2
        STA ZP_JFLAG
        RTS

; ---------------------------------------------------------------------------
; IF ... THEN ... ELSE
; ---------------------------------------------------------------------------

DO_IF:
        JSR EXPR
        REP #$20
        TXA
        CMP.W #0
        SEP #$20
        BEQ DO_IF_FALSE
        LDX.W #(KW_THEN & $FFFF)
        LDY.W #KW_THEN_END - KW_THEN
        JSR MATCHKW
        BCC DO_IF_NOTTHEN
        LDA [ZP_SRC]
        CMP #'0'
        BCC DO_IF_REST
        CMP #'9'+1
        BCS DO_IF_REST
        JMP DO_GOTO             ; `IF x THEN 100`
DO_IF_REST:
        RTS
DO_IF_NOTTHEN:
        JSR SAY_WHAT
        LDA #3
        STA ZP_JFLAG
        RTS
DO_IF_FALSE:
DO_IF_SKIP:
        JSR SKIPSP
        LDA [ZP_SRC]
        BEQ DO_IF_SKIP_END
        JSR TO_UPPER
        CMP #'E'
        BEQ DO_IF_TRY_ELSE
        JSR SRC_STEP
        BRA DO_IF_SKIP
DO_IF_TRY_ELSE:
        LDX.W #(KW_ELSE & $FFFF)
        LDY.W #KW_ELSE_END - KW_ELSE
        JSR MATCHKW
        BCS DO_IF_SKIP_END
        JSR SRC_STEP
        BRA DO_IF_SKIP
DO_IF_SKIP_END:
        RTS

; ELSE reached by a branch that ran: the rest of the line belongs to the other
; branch, so it is skipped rather than run.
DO_ELSE:
DO_ELSE_LOOP:
        LDA [ZP_SRC]
        BEQ DO_ELSE_DONE
        JSR SRC_STEP
        BRA DO_ELSE_LOOP
DO_ELSE_DONE:
        RTS

; ---------------------------------------------------------------------------
; FOR and NEXT
; ---------------------------------------------------------------------------
;
; A frame is ten bytes at FOR_BASE: the variable, the step, the limit, where the
; body's text is, and how far into it to come back to. Nothing is kept on the
; machine stack: the three expressions are evaluated into zero page and copied in
; one at a time, which is one fewer thing to get unbalanced.

; FOR_PTR: ZP_FORSP is the count, so X = where the next frame goes.
FOR_PTR:
        LDA ZP_FORSP
        JMP FOR_INDEX_PTR

DO_FOR:
        JSR SKIPSP
        JSR IS_ALPHA
        BCC DO_FOR_BAD
        LDA [ZP_SRC]
        JSR TO_UPPER
        SEC
        SBC #'A'
        STA ZP_VAR
        JSR SRC_STEP
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'='
        BEQ DO_FOR_EQ
        BRA DO_FOR_BAD
DO_FOR_EQ:
        JSR SRC_STEP
        JSR EXPR
        JSR STORE_VAR
        LDX.W #(KW_TO & $FFFF)
        LDY.W #KW_TO_END - KW_TO
        JSR MATCHKW
        BCC DO_FOR_BAD
        JSR EXPR
        STX ZP_LIMIT            ; its own word: EXPR's scratch is not free
        LDX.W #(KW_STEP & $FFFF)
        LDY.W #KW_STEP_END - KW_STEP
        JSR MATCHKW
        BCC DO_FOR_DEFAULT_STEP
        JSR EXPR
        REP #$20
        CPX.W #0
        SEP #$20
        BEQ DO_FOR_BAD
        STX ZP_NEWLEN
        BRA DO_FOR_TEST
DO_FOR_DEFAULT_STEP:
        REP #$20
        LDA.W #1
        STA ZP_NEWLEN
        SEP #$20
        ; A loop whose start is already past its limit has no passes in it at
        ; all -- `FOR I=5 TO 1` runs nothing -- and the 1982 machine already
        ; answered that way. The body is skipped to the NEXT that matches this
        ; FOR, and no frame goes on the stack, because there is nothing to come
        ; back to.
DO_FOR_TEST:
        LDA ZP_VAR
        JSR LOAD_VAR            ; X = the start, which STORE_VAR put away
        REP #$20
        TXA
        EOR.W #$8000
        STA ZP_VAL              ; the start, biased into unsigned order
        LDA ZP_LIMIT
        EOR.W #$8000
        STA ZP_A                ; and the limit, the same way
        LDA ZP_NEWLEN
        BMI DO_FOR_DOWN
        LDA ZP_VAL
        CMP ZP_A
        SEP #$20
        BCC DO_FOR_FRAME        ; below the limit: the body runs
        BEQ DO_FOR_FRAME        ; at it: one pass
        BRA DO_FOR_SKIP
DO_FOR_DOWN:
        LDA ZP_VAL
        CMP ZP_A
        SEP #$20
        BCS DO_FOR_FRAME        ; above the limit, counting down: it runs
        BEQ DO_FOR_FRAME
DO_FOR_SKIP:
        JMP FOR_SKIP
DO_FOR_FRAME:
        LDA ZP_FORSP
        CMP #FOR_MAX
        BCS DO_FOR_FULL
        JSR TEXT_OFFSET
        JSR FOR_PTR             ; X = the new frame's offset
        LDA ZP_VAR
        STA FOR_BASE,X
        REP #$20
        INX
        INX
        LDA ZP_NEWLEN
        STA FOR_BASE,X          ; step
        INX
        INX
        LDA ZP_LIMIT
        STA FOR_BASE,X          ; limit
        INX
        INX
        LDA ZP_BASE
        STA FOR_BASE,X          ; the text
        INX
        INX
        LDA ZP_OFF
        STA FOR_BASE,X          ; where the body starts
        SEP #$20
        INC ZP_FORSP
        RTS
DO_FOR_FULL:
        JSR SAY_NOROOM
        LDA #3
        STA ZP_JFLAG
        RTS
DO_FOR_BAD:
        JSR SAY_WHAT
        LDA #3
        STA ZP_JFLAG
        RTS

; FOR_SKIP: the text after a FOR statement that has no passes in it, skipped to
; the NEXT that matches it, with ZP_SRC left after that NEXT and its variable.
;
; The scan counts FORs and NEXTs as words at statement boundaries, steps over
; quoted literals because the letters inside one are not statements, and treats
; a remark as the rest of its line. A line ends at its terminator, past which
; the next record's number and text begin; a zero number there is the end of the
; program, and then there is no NEXT to find.
FOR_SKIP:
        REP #$20
        LDA.W #1
        STA ZP_TMP16            ; how many FORs deep the scan is
        SEP #$20
FOR_SKIP_LOOP:
        JSR SKIPSP
        LDA [ZP_SRC]
        BNE FOR_SKIP_TOKEN
        BRA FOR_SKIP_NEXTLINE
FOR_SKIP_TOKEN:
        CMP #'"'
        BEQ FOR_SKIP_STRING
        LDX.W #(KW_REM & $FFFF)
        LDY.W #KW_REM_END - KW_REM
        JSR MATCHKW
        BCS FOR_SKIP_REMARK
        LDX.W #(KW_FOR & $FFFF)
        LDY.W #KW_FOR_END - KW_FOR
        JSR MATCHKW
        BCS FOR_SKIP_DEEPER
        LDX.W #(KW_NEXT & $FFFF)
        LDY.W #KW_NEXT_END - KW_NEXT
        JSR MATCHKW
        BCS FOR_SKIP_SHALLOWER
        JSR SRC_STEP
        BRA FOR_SKIP_LOOP
FOR_SKIP_DEEPER:
        REP #$20
        INC ZP_TMP16
        SEP #$20
        BRA FOR_SKIP_LOOP
FOR_SKIP_SHALLOWER:
        REP #$20
        DEC ZP_TMP16
        LDA ZP_TMP16
        SEP #$20
        BNE FOR_SKIP_LOOP
        JSR IS_ALPHA            ; `NEXT I` names its variable; skip it too
        BCC FOR_SKIP_DONE
        JSR SRC_STEP
FOR_SKIP_DONE:
        RTS
FOR_SKIP_STRING:
        JSR SRC_STEP            ; past the opening quote
FOR_SKIP_STRING_LOOP:
        LDA [ZP_SRC]
        BEQ FOR_SKIP_NEXTLINE   ; a quote that never closes ends its line
        CMP #'"'
        BEQ FOR_SKIP_STRING_END
        JSR SRC_STEP
        BRA FOR_SKIP_STRING_LOOP
FOR_SKIP_STRING_END:
        JSR SRC_STEP
        BRA FOR_SKIP_LOOP
FOR_SKIP_REMARK:
        LDA [ZP_SRC]            ; the rest of the line is prose, not statements
        BEQ FOR_SKIP_NEXTLINE
        JSR SRC_STEP
        BRA FOR_SKIP_REMARK
FOR_SKIP_NEXTLINE:
        ; A FOR typed at the prompt has no next line to walk into: past the
        ; prompt's terminator is the line buffer, not a program. The prompt holds
        ; a loop only as far as the end of the line it is typed on.
        REP #$20
        LDA ZP_LINE
        CMP.W #$FFFF
        SEP #$20
        BEQ FOR_SKIP_NONEXT
        REP #$20
        LDA ZP_SRC
        INC A                   ; past the terminator: the next record's number
        TAX
        SEP #$20
        LDA $0000,X
        BNE FOR_SKIP_NUMBER
        LDA $0001,X
        BNE FOR_SKIP_NUMBER
        BRA FOR_SKIP_NONEXT     ; a zero number: there is no next record
FOR_SKIP_NUMBER:
        REP #$20
        TXA
        CLC
        ADC.W #2
        STA ZP_SRC              ; past the number: the next line's text
        SEP #$20
        LDA #0
        STA ZP_SRC+2
        BRA FOR_SKIP_LOOP
FOR_SKIP_NONEXT:
        JSR SAY_FORNEXT
        LDA #3
        STA ZP_JFLAG
        RTS

DO_NEXT:
        JSR SKIPSP
        LDA #$FF
        STA ZP_VAR              ; $FF: no variable was named
        JSR IS_ALPHA
        BCC DO_NEXT_FIND
        LDA [ZP_SRC]
        JSR TO_UPPER
        SEC
        SBC #'A'
        STA ZP_VAR
        JSR SRC_STEP
DO_NEXT_FIND:
        LDA ZP_FORSP
        BEQ DO_NEXT_NONE
        DEC A
        STA ZP_NEWLEN           ; the frame under consideration
DO_NEXT_SEARCH:
        LDA ZP_NEWLEN
        JSR FOR_INDEX_PTR
        LDA FOR_BASE,X
        CMP ZP_VAR
        BEQ DO_NEXT_HAVE
        LDA ZP_VAR
        CMP #$FF
        BEQ DO_NEXT_HAVE
        LDA ZP_NEWLEN
        BEQ DO_NEXT_NONE
        DEC A
        STA ZP_NEWLEN
        BRA DO_NEXT_SEARCH
DO_NEXT_NONE:
        JSR SAY_NEXTFOR
        LDA #3
        STA ZP_JFLAG
        RTS
DO_NEXT_HAVE:
        REP #$20
        TXA
        STA ZP_TMP16            ; the frame's offset
        LDA ZP_NEWLEN
        AND.W #$00FF
        STA ZP_OFF              ; the frame's index, for when it is finished
        SEP #$20
        LDA FOR_BASE,X
        JSR LOAD_VAR            ; X = the variable's value
        REP #$20
        TXA
        STA ZP_VAL
        LDX ZP_TMP16
        LDA FOR_BASE+2,X        ; the step
        CLC
        ADC ZP_VAL
        STA ZP_VAL              ; the value this pass has reached
        LDA FOR_BASE,X
        AND.W #$00FF
        ASL A
        TAY
        LDA ZP_VAL
        STA VARS,Y
        ; the test, signed: both sides biased by $8000
        EOR.W #$8000
        STA ZP_CNT2             ; the value, biased
        LDA FOR_BASE+4,X        ; the limit
        EOR.W #$8000
        STA ZP_NEWLEN           ; the limit, biased
        LDA FOR_BASE+2,X        ; the step decides which end ends it
        BMI DO_NEXT_NEGATIVE
        LDA ZP_CNT2
        CMP ZP_NEWLEN
        BEQ DO_NEXT_LOOP
        BCC DO_NEXT_LOOP        ; still at or below the limit
        BRA DO_NEXT_FINISH
DO_NEXT_NEGATIVE:
        LDA ZP_CNT2
        CMP ZP_NEWLEN
        BEQ DO_NEXT_LOOP
        BCS DO_NEXT_LOOP        ; still at or above it
        BRA DO_NEXT_FINISH
DO_NEXT_LOOP:
        LDA FOR_BASE+6,X        ; the body, again
        CLC
        ADC FOR_BASE+8,X
        STA ZP_SRC
        SEP #$20
        LDA #BANK_KERNAL
        STA ZP_SRC+2
        LDA #2
        STA ZP_JFLAG
        RTS
DO_NEXT_FINISH:
        SEP #$20
        LDA ZP_OFF
        STA ZP_FORSP            ; this frame goes
        RTS

; FOR_INDEX_PTR: A = a frame's index; X = that frame's offset.
;
; A frame is ten bytes: the variable, a word each for the step, the limit, the
; body's text and how far into it to come back to. The stride has to be those
; ten -- it was eight plus one, which put a nested loop's frame one byte into its
; parent's, and the parent's body offset lost its high byte the moment the inner
; loop started.
FOR_INDEX_PTR:
        REP #$20
        AND.W #$00FF
        ASL A
        STA ZP_CNT2             ; twice the frame's number
        ASL A
        ASL A                   ; eight times
        CLC
        ADC ZP_CNT2             ; ten, which is the frame
        TAX
        SEP #$20
        RTS
