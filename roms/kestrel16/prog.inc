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

; CLEAR_VARS: every variable is zero, as after NEW or RUN.
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
