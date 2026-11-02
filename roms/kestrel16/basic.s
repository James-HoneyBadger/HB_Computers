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
; What is here: stored programs, integer expressions, variables A to Z, PRINT
; with strings and separators, LET, branches, subroutines, FOR/NEXT with STEP,
; and the disk commands FORMAT, CATALOG, SAVE, LOAD and DELETE. Strings as
; values, arrays and functions are still beyond this language.

        .include "kestrel16.inc"

        .org BASIC

; The language's own direct page, above the KERNAL's. $00-$0F is the expression
; scratch the map keeps for a language; the strings use the bottom of it.
ZP_SSTR         = $00           ; 24-bit: the string being read
ZP_SLEN         = $03           ; 16-bit: how many characters it has
ZP_SDST         = $05           ; 24-bit: the slot an assignment is writing
ZP_ARR          = $08           ; a byte: the letter an array operation is about
ZP_SSTR2        = $09           ; 24-bit: the left side of a string comparison
ZP_SLEN2        = $0C           ; 16-bit: and how long it is
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
ZP_BAD          = $44           ; an expression held something unreadable
ZP_SGN          = $45           ; 16-bit: 1 when the quotient's sign is negative
ZP_STATE        = $46           ; 0 at the prompt, 1 running a program
ZP_JFLAG        = $47           ; a statement moved control: 1 line, 2 resume, 3 stop
ZP_TMP16        = $48           ; 16-bit scratch
ZP_LINE         = $4A           ; the address of the line being run, $FFFF at the prompt
ZP_LNUM         = $4C           ; 16-bit, the line's number
ZP_DLINE        = $4E           ; where the prompt's own line starts
ZP_PROGTOP      = $50           ; one past the last byte of the program
ZP_FORSP        = $52
ZP_GSUBSP       = $53
ZP_PP           = $54           ; a 24-bit pointer into the program
ZP_QP           = $57           ; a second one
ZP_CNT2         = $5A           ; 16-bit scratch
ZP_NEWLEN       = $5C           ; 16-bit, the record being written
ZP_TMPLEN       = $5E           ; 16-bit, a text's length
ZP_BASE         = $60           ; 16-bit: where the running line's text starts
ZP_OFF          = $62           ; 16-bit: how far into it a statement begins
ZP_LIMIT        = $64           ; 16-bit: a FOR's limit, safe from EXPR's scratch

FOR_BASE        = $7E00         ; FOR frames, ten bytes each
FOR_MAX         = 6
GOSUB_BASE      = $7EC0         ; GOSUB frames, four bytes each
GOSUB_MAX       = 16

VARS            = $0007C0       ; twenty-six words, A to Z

; The name a SAVE, LOAD or DELETE is about, in bank zero: the filesystem takes a
; sixteen-bit pointer to it, so it has to live where a sixteen-bit pointer can
; reach it, and it has to still be there after the read the call makes.
DOSNAME         = $000700       ; sixteen bytes, upper case, space padded

ENTRY:
        SEP #$20
        REP #$10
        LDA #LIGHTGREY
        STA IO_INK
        LDA #BLACK
        STA IO_PAPER
        JSL L_CLS
        JSR SAY_BANNER
        JSR PROG_NEW
        JSR SAY_NEWLINE          ; the banner has no newline of its own
MAIN_LOOP:
        JSR SAY_READY
MAIN_QUIET:
        JSR SAY_NEWLINE          ; the line is typed on the one below READY.
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
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'0'
        BCC MAIN_EXECUTE
        CMP #'9'+1
        BCS MAIN_EXECUTE
        JSR STORE_INPUT
        BRA MAIN_QUIET          ; storing a line is not a command: no prompt
MAIN_EXECUTE:
        JSR EXEC_LINE
        BRA MAIN_LOOP

; STORE_INPUT: a line that begins with a number is a program line, not something
; to run: the number is read, and what follows it is what will run later.
STORE_INPUT:
        REP #$20
        LDA.W #0
        STA ZP_TMP16
        SEP #$20
STORE_NUM_LOOP:
        LDA [ZP_SRC]
        CMP #'0'
        BCC STORE_NUM_DONE
        CMP #'9'+1
        BCS STORE_NUM_DONE
        SEC
        SBC #'0'
        STA ZP_CH
        REP #$20
        LDA ZP_TMP16
        ASL A
        STA ZP_CNT2
        ASL A
        ASL A
        CLC
        ADC ZP_CNT2             ; ten times
        STA ZP_TMP16
        SEP #$20
        LDA ZP_CH
        REP #$20
        AND.W #$00FF
        CLC
        ADC ZP_TMP16
        STA ZP_TMP16
        SEP #$20
        JSR SRC_STEP
        BRA STORE_NUM_LOOP
STORE_NUM_DONE:
        JMP STORE_LINE

; ---------------------------------------------------------------------------
; The messages
; ---------------------------------------------------------------------------

TXT_BANNER:
        .text "KESTREL 16 BASIC 2.0"
TXT_BANNER_END:
TXT_READY:
        .text "READY."
TXT_READY_END:
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
KW_FORMAT:
        .text "FORMAT"
KW_FORMAT_END:
KW_CATALOG:
        .text "CATALOG"
KW_CATALOG_END:
KW_DIR:
        .text "DIR"
KW_DIR_END:
KW_SAVE:
        .text "SAVE"
KW_SAVE_END:
KW_LOAD:
        .text "LOAD"
KW_LOAD_END:
KW_DELETE:
        .text "DELETE"
KW_DELETE_END:
KW_REM:
        .text "REM"
KW_REM_END:
KW_END:
        .text "END"
KW_END_END:
KW_NEW:
        .text "NEW"
KW_NEW_END:
KW_RUN:
        .text "RUN"
KW_RUN_END:
KW_LIST:
        .text "LIST"
KW_LIST_END:
KW_GOTO:
        .text "GOTO"
KW_GOTO_END:
KW_IF:
        .text "IF"
KW_IF_END:
KW_THEN:
        .text "THEN"
KW_THEN_END:
KW_ELSE:
        .text "ELSE"
KW_ELSE_END:
KW_GOSUB:
        .text "GOSUB"
KW_GOSUB_END:
KW_RETURN:
        .text "RETURN"
KW_RETURN_END:
KW_FOR:
        .text "FOR"
KW_FOR_END:
KW_TO:
        .text "TO"
KW_TO_END:
KW_STEP:
        .text "STEP"
KW_STEP_END:
KW_NEXT:
        .text "NEXT"
KW_NEXT_END:
KW_LEN:
        .text "LEN"
KW_LEN_END:
KW_DIM:
        .text "DIM"
KW_DIM_END:
KW_AND:
        .text "AND"
KW_AND_END:
KW_OR:
        .text "OR"
KW_OR_END:
KW_NOT:
        .text "NOT"
KW_NOT_END:
TXT_NEXTFOR:
        .text "?NEXT WITHOUT FOR"
TXT_NEXTFOR_END:
TXT_FORNEXT:
        .text "?FOR WITHOUT NEXT"
TXT_FORNEXT_END:
TXT_RETGOSUB:
        .text "?RETURN WITHOUT GOSUB"
TXT_RETGOSUB_END:
TXT_NOLINE:
        .text "?NO LINE"
TXT_NOLINE_END:
TXT_NOROOM:
        .text "?NO ROOM"
TXT_NOROOM_END:
TXT_SAVED:
        .text "SAVED"
TXT_SAVED_END:
TXT_LOADED:
        .text "LOADED"
TXT_LOADED_END:
TXT_DELETED:
        .text "DELETED"
TXT_DELETED_END:
TXT_NONAME:
        .text "?FILENAME"
TXT_NONAME_END:
TXT_MISMATCH:
        .text "?TYPE MISMATCH"
TXT_MISMATCH_END:
TXT_TOOLONG:
        .text "?STRING TOO LONG"
TXT_TOOLONG_END:
TXT_SUBSCRIPT:
        .text "?SUBSCRIPT"
TXT_SUBSCRIPT_END:

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
SAY_NOLINE:
        LDA #<TXT_NOLINE
        STA ZP_PTR
        LDA #>TXT_NOLINE
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_NOLINE_END - TXT_NOLINE
        JSL L_PRINT
        JSR SAY_NEWLINE
        RTS
SAY_NOROOM:
        LDA #<TXT_NOROOM
        STA ZP_PTR
        LDA #>TXT_NOROOM
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_NOROOM_END - TXT_NOROOM
        JSL L_PRINT
        JSR SAY_NEWLINE
        RTS
SAY_SAVED:
        LDA #<TXT_SAVED
        STA ZP_PTR
        LDA #>TXT_SAVED
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_SAVED_END - TXT_SAVED
        JSL L_PRINT
        JSR SAY_NEWLINE
        RTS
SAY_LOADED:
        LDA #<TXT_LOADED
        STA ZP_PTR
        LDA #>TXT_LOADED
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_LOADED_END - TXT_LOADED
        JSL L_PRINT
        JSR SAY_NEWLINE
        RTS
SAY_DELETED:
        LDA #<TXT_DELETED
        STA ZP_PTR
        LDA #>TXT_DELETED
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_DELETED_END - TXT_DELETED
        JSL L_PRINT
        JSR SAY_NEWLINE
        RTS
SAY_NONAME:
        LDA #<TXT_NONAME
        STA ZP_PTR
        LDA #>TXT_NONAME
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_NONAME_END - TXT_NONAME
        JSL L_PRINT
        JSR SAY_NEWLINE
        RTS
SAY_MISMATCH:
        LDA #<TXT_MISMATCH
        STA ZP_PTR
        LDA #>TXT_MISMATCH
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_MISMATCH_END - TXT_MISMATCH
        JSL L_PRINT
        JSR SAY_NEWLINE
        RTS
SAY_TOOLONG:
        LDA #<TXT_TOOLONG
        STA ZP_PTR
        LDA #>TXT_TOOLONG
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_TOOLONG_END - TXT_TOOLONG
        JSL L_PRINT
        JSR SAY_NEWLINE
        RTS
SAY_SUBSCRIPT:
        LDA #<TXT_SUBSCRIPT
        STA ZP_PTR
        LDA #>TXT_SUBSCRIPT
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_SUBSCRIPT_END - TXT_SUBSCRIPT
        JSL L_PRINT
        JSR SAY_NEWLINE
        RTS
SAY_NEXTFOR:
        LDA #<TXT_NEXTFOR
        STA ZP_PTR
        LDA #>TXT_NEXTFOR
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_NEXTFOR_END - TXT_NEXTFOR
        JSL L_PRINT
        JSR SAY_NEWLINE
        RTS
SAY_FORNEXT:
        LDA #<TXT_FORNEXT
        STA ZP_PTR
        LDA #>TXT_FORNEXT
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_FORNEXT_END - TXT_FORNEXT
        JSL L_PRINT
        JSR SAY_NEWLINE
        RTS
SAY_RETGOSUB:
        LDA #<TXT_RETGOSUB
        STA ZP_PTR
        LDA #>TXT_RETGOSUB
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_RETGOSUB_END - TXT_RETGOSUB
        JSL L_PRINT
        JSR SAY_NEWLINE
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

; SRC_STEP: ZP_SRC goes on by one, in sixteen bits.
;
; A memory increment is as wide as the accumulator, and the text is read in
; eight-bit mode, where `INC ZP_SRC` steps the low byte and carries into
; nothing: a statement or a string that crossed a page boundary put the pointer
; back at the top of the page it was already on, and the machine read its own
; line for ever. Called with the accumulator eight bits wide, as everything
; between statements is.
SRC_STEP:
        REP #$20
        LDA ZP_SRC
        INC A
        STA ZP_SRC
        SEP #$20
        RTS

SKIPSP:
        PHP
        SEP #$20
SKIPSP_LOOP:
        LDA [ZP_SRC]
        CMP #32
        BNE SKIPSP_DONE
        JSR SRC_STEP
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
;
; X is the keyword's whole sixteen-bit address and not its low byte: `#<KW_x`
; fits in a byte and reads as though it were one, and it only works for as long
; as the table stays inside the bank's first page. It stopped being true the day
; the table grew past it, and every keyword past the first page became `?WHAT?`.
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
; EXEC_LINE: run the line at ZP_SRC.
;
; A line is statements separated by colons, run in order, which is what a program
; line is too -- so the same loop serves the prompt and a running program, and
; ZP_STATE is the only thing that differs: at the prompt a line ends at its last
; statement, and in a program it ends at the next line.
; ---------------------------------------------------------------------------

EXEC_LINE:
        LDA #0
        STA ZP_STATE
        STA ZP_JFLAG
        JSR RESET_STACKS
        REP #$20
        LDA ZP_SRC
        STA ZP_DLINE            ; the prompt's own text
        LDA.W #$FFFF
        STA ZP_LINE             ; not a program line: the line ends the run
        SEP #$20
EXEC_SEQ:
        JSR SKIPSP
        LDA [ZP_SRC]
        BEQ EXEC_SEQ_END
        CMP #':'
        BNE EXEC_SEQ_ONE
        JSR SRC_STEP
        BRA EXEC_SEQ
EXEC_SEQ_ONE:
        JSR EXEC_STMT
        LDA ZP_JFLAG
        BNE EXEC_CONTROL
        BRA EXEC_SEQ
EXEC_CONTROL:
        LDA ZP_JFLAG
        CMP #3
        BEQ EXEC_STOP
        CMP #2
        BEQ EXEC_RESUME
        LDA #0
        STA ZP_JFLAG
        LDA #1
        STA ZP_STATE            ; a jump from the prompt starts the program
        JMP RUN_LINE
EXEC_RESUME:
        LDA #0
        STA ZP_JFLAG
        LDA #1
        STA ZP_STATE
        JMP EXEC_SEQ
EXEC_STOP:
        LDA #0
        STA ZP_JFLAG
        STA ZP_STATE
        RTS
EXEC_SEQ_END:
        REP #$20
        LDA ZP_LINE
        CMP.W #$FFFF
        SEP #$20
        BNE EXEC_NEXT_LINE
        RTS
EXEC_NEXT_LINE:
        JSR SKIPSP
        REP #$20
        LDA ZP_SRC              ; the terminator: the next record starts after it
        INC A
        STA ZP_LINE
        SEP #$20
        BRA RUN_LINE

; RUN_LINE: begin the program line at ZP_LINE, or finish if that is the end mark.
RUN_LINE:
        REP #$20
        LDX ZP_LINE
        LDA $0000,X             ; the line's number; zero is the end of the program
        BEQ RUN_FINISHED
        STA ZP_LNUM
        TXA
        CLC
        ADC.W #2
        STA ZP_SRC              ; the text, past the number
        SEP #$20
        LDA #0
        STA ZP_SRC+2
        BRA EXEC_SEQ
RUN_FINISHED:
        SEP #$20
        JSR SAY_NEWLINE
        RTS

; ---------------------------------------------------------------------------
; EXEC_STMT: run one statement at ZP_SRC.
; ---------------------------------------------------------------------------

EXEC_STMT:
        JSR SKIPSP
        LDA [ZP_SRC]
        BEQ EXEC_DONE
        LDX.W #(KW_PRINT & $FFFF)
        LDY.W #KW_PRINT_END - KW_PRINT
        JSR MATCHKW
        BCS DO_PRINT
        LDA [ZP_SRC]
        CMP #'?'
        BNE EXEC_TRY_LET
        JSR SRC_STEP
        JSR SKIPSP
        BRA DO_PRINT
EXEC_TRY_LET:
        LDX.W #(KW_LET & $FFFF)
        LDY.W #KW_LET_END - KW_LET
        JSR MATCHKW
        BCS DO_LET
        LDX.W #(KW_CLS & $FFFF)
        LDY.W #KW_CLS_END - KW_CLS
        JSR MATCHKW
        BCS DO_CLS
        LDX.W #(KW_FORMAT & $FFFF)
        LDY.W #KW_FORMAT_END - KW_FORMAT
        JSR MATCHKW
        BCS DO_FORMAT
        LDX.W #(KW_CATALOG & $FFFF)
        LDY.W #KW_CATALOG_END - KW_CATALOG
        JSR MATCHKW
        BCS DO_CATALOG
        LDX.W #(KW_DIR & $FFFF)
        LDY.W #KW_DIR_END - KW_DIR
        JSR MATCHKW
        BCS DO_CATALOG
        LDX.W #(KW_SAVE & $FFFF)
        LDY.W #KW_SAVE_END - KW_SAVE
        JSR MATCHKW
        BCS DO_SAVE
        LDX.W #(KW_LOAD & $FFFF)
        LDY.W #KW_LOAD_END - KW_LOAD
        JSR MATCHKW
        BCS DO_LOAD
        LDX.W #(KW_DELETE & $FFFF)
        LDY.W #KW_DELETE_END - KW_DELETE
        JSR MATCHKW
        BCS DO_DELETE
        LDX.W #(KW_REM & $FFFF)
        LDY.W #KW_REM_END - KW_REM
        JSR MATCHKW
        BCS DO_REM
        LDX.W #(KW_DIM & $FFFF)
        LDY.W #KW_DIM_END - KW_DIM
        JSR MATCHKW
        BCS DO_DIM
        LDX.W #(KW_NEW & $FFFF)
        LDY.W #KW_NEW_END - KW_NEW
        JSR MATCHKW
        BCS DO_NEW
        LDX.W #(KW_RUN & $FFFF)
        LDY.W #KW_RUN_END - KW_RUN
        JSR MATCHKW
        BCS DO_RUN
        LDX.W #(KW_LIST & $FFFF)
        LDY.W #KW_LIST_END - KW_LIST
        JSR MATCHKW
        BCS DO_LIST
        LDX.W #(KW_GOTO & $FFFF)
        LDY.W #KW_GOTO_END - KW_GOTO
        JSR MATCHKW
        BCS DO_GOTO
        LDX.W #(KW_IF & $FFFF)
        LDY.W #KW_IF_END - KW_IF
        JSR MATCHKW
        BCS DO_IF
        LDX.W #(KW_GOSUB & $FFFF)
        LDY.W #KW_GOSUB_END - KW_GOSUB
        JSR MATCHKW
        BCS DO_GOSUB
        LDX.W #(KW_RETURN & $FFFF)
        LDY.W #KW_RETURN_END - KW_RETURN
        JSR MATCHKW
        BCS DO_RETURN
        LDX.W #(KW_FOR & $FFFF)
        LDY.W #KW_FOR_END - KW_FOR
        JSR MATCHKW
        BCS DO_FOR
        LDX.W #(KW_NEXT & $FFFF)
        LDY.W #KW_NEXT_END - KW_NEXT
        JSR MATCHKW
        BCS DO_NEXT
        LDX.W #(KW_ELSE & $FFFF)
        LDY.W #KW_ELSE_END - KW_ELSE
        JSR MATCHKW
        BCS DO_ELSE
        LDX.W #(KW_END & $FFFF)
        LDY.W #KW_END_END - KW_END
        JSR MATCHKW
        BCS DO_END
        JSR IS_ALPHA
        BCS DO_LET
        JSR SAY_WHAT
        BRA DO_REM               ; skip the rest of the line, so nothing spins
DO_NEW:
        JSR PROG_NEW
        JMP EXEC_DONE
; A remark is the rest of the line, colons and all, and it is skipped rather
; than executed: a statement that consumes nothing would be run again for ever.
DO_REM:
DO_REM_LOOP:
        LDA [ZP_SRC]
        BEQ DO_REM_DONE
        JSR SRC_STEP
        BRA DO_REM_LOOP
DO_REM_DONE:
        JMP EXEC_DONE
DO_FORMAT:
        JSL L_DOSFORMAT
        BRA EXEC_DONE
DO_CATALOG:
        JSL L_DOSCATALOG
        BRA EXEC_DONE

; SAVE, LOAD and DELETE all begin with a quoted name, which goes into DOSNAME in
; bank zero -- the filesystem takes a sixteen-bit pointer to it, and it has to
; outlive the read the call makes. On failure the filesystem has already said
; what was wrong (`?DISK`, `?NOT FOUND`, `?DISK FULL`), so the language only says
; what it did when it did it.
DO_SAVE:
        JSR PARSEFN
        BCS SAVE_NONAME
        REP #$20
        LDA.W #DOSNAME
        SEP #$20
        JSL L_DOSSAVE
        CMP #0
        BNE EXEC_DONE
        JSR SAY_SAVED
        BRA EXEC_DONE
SAVE_NONAME:
        JSR SAY_NONAME
        BRA EXEC_DONE
DO_LOAD:
        JSR PARSEFN
        BCS LOAD_NONAME
        REP #$20
        LDA.W #DOSNAME
        SEP #$20
        JSL L_DOSLOAD
        CMP #0
        BNE EXEC_DONE
        ; The program that was running is not there any more: its text was
        ; overwritten by the file, so the line stops rather than running on into
        ; whatever came back.
        LDA #3
        STA ZP_JFLAG
        JSR RESET_STACKS
        JSR SAY_LOADED
        BRA EXEC_DONE
LOAD_NONAME:
        JSR SAY_NONAME
        BRA EXEC_DONE
DO_DELETE:
        JSR PARSEFN
        BCS DELETE_NONAME
        REP #$20
        LDA.W #DOSNAME
        SEP #$20
        JSL L_DOSDELETE
        CMP #0
        BNE EXEC_DONE
        JSR SAY_DELETED
        BRA EXEC_DONE
DELETE_NONAME:
        JSR SAY_NONAME
        BRA EXEC_DONE

; PARSEFN: the quoted name after SAVE, LOAD or DELETE goes to DOSNAME, upper
; case and padded with spaces to the sixteen bytes the directory holds. Carry is
; set when there was no quoted name at all.
PARSEFN:
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'"'
        BNE PARSEFN_NONE
        JSR SRC_STEP
        LDX.W #0
        LDA #32
PARSEFN_BLANK:
        STA.L DOSNAME,X         ; bank zero, whatever the data bank is
        INX
        CPX.W #16
        BNE PARSEFN_BLANK
        LDX.W #0
PARSEFN_LOOP:
        LDA [ZP_SRC]
        BEQ PARSEFN_DONE
        CMP #'"'
        BEQ PARSEFN_CLOSE
        CPX.W #16
        BCS PARSEFN_SKIP        ; a name may be longer; the directory's is not
        JSR TO_UPPER
        STA.L DOSNAME,X
        INX
PARSEFN_SKIP:
        JSR SRC_STEP
        BRA PARSEFN_LOOP
PARSEFN_CLOSE:
        JSR SRC_STEP
PARSEFN_DONE:
        CLC
        RTS
PARSEFN_NONE:
        SEC
        RTS

DO_CLS:
        JSL L_CLS
        JSR SAY_BANNER
        JSR SAY_NEWLINE
        BRA EXEC_DONE
DO_END:
        LDA #3                  ; END stops a program; at the prompt it just ends the line
        STA ZP_JFLAG
        JMP EXEC_DONE
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
        STA ZP_ARR
        JSR SRC_STEP
        LDA [ZP_SRC]
        CMP #'$'                ; `A$` is a string, and `A $` is not
        BEQ ASSIGN_STR
        CMP #'('                ; `A(3)=1` writes one element of an array
        BEQ ASSIGN_ARRAY
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'='
        BEQ ASSIGN_STORE
        JSR SAY_WHAT
        RTS
ASSIGN_STR:
        JSR SRC_STEP            ; past the dollar
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'='
        BEQ ASSIGN_STR_STORE
        JSR SAY_WHAT
        RTS
ASSIGN_STR_STORE:
        JSR SRC_STEP            ; past the equals
        JSR PARSE_STR
        BCC ASSIGN_MISMATCH     ; a number cannot go into a string
        JSR STR_STORE
        RTS
ASSIGN_MISMATCH:
        JSR SAY_MISMATCH
        LDA #3                  ; the line stops here, as it does on ?WHAT?
        STA ZP_JFLAG
        RTS
ASSIGN_ARRAY:
        JSR SRC_STEP            ; past the opening bracket
        JSR EXPR                ; X = the subscript
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE ASSIGN_SUBSCRIPT
        JSR SRC_STEP
        LDA ZP_VAR              ; the letter, which the subscript cannot take
        STA ZP_ARR
        JSR ARR_ELEM            ; ZP_SDST = the element, and X survives it
        BCS ASSIGN_SUBSCRIPT
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'='
        BEQ ASSIGN_ARRAY_STORE
        JSR SAY_WHAT
        RTS
ASSIGN_ARRAY_STORE:
        JSR SRC_STEP            ; past the equals
        JSR EXPR                ; X = the value, which uses none of ZP_SDST
        REP #$20
        TXA
        STA [ZP_SDST]
        SEP #$20
        RTS
ASSIGN_SUBSCRIPT:
        JSR SAY_SUBSCRIPT
        LDA #3
        STA ZP_JFLAG
        RTS
ASSIGN_STORE:
        JSR SRC_STEP
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
        STA ZP_BAD
PRINT_LOOP:
        JSR SKIPSP
        LDA [ZP_SRC]
        BEQ PRINT_END
        ; A colon ends the statement, not the item: `PRINT "A": PRINT "B"` is
        ; two statements on one line, and the next one starts after the colon.
        CMP #':'
        BEQ PRINT_END
        CMP #'"'
        BEQ PRINT_STRING
        CMP #';'
        BEQ PRINT_SEMI
        CMP #','
        BEQ PRINT_COMMA
        ; `IF x THEN PRINT "A" ELSE PRINT "B"`: the ELSE belongs to the line and
        ; not to the items, so printing stops before it and the dispatcher takes
        ; it, which is what skips the other branch.
        LDX.W #(KW_ELSE & $FFFF)
        LDY.W #KW_ELSE_END - KW_ELSE
        JSR MATCHKW
        BCC PRINT_NOT_ELSE
        LDA ZP_SAVE             ; MATCHKW kept the pointer: put it back
        STA ZP_SRC
        LDA ZP_SAVE+1
        STA ZP_SRC+1
        LDA ZP_SAVE+2
        STA ZP_SRC+2
        JMP PRINT_END
PRINT_NOT_ELSE:
        ; `PRINT A$="X"` is a value, not two items. An item still has to consume
        ; something, or the line would print a zero a time for ever, which is
        ; what `PRINT A$` did before strings existed.
        LDA ZP_SRC              ; where the item starts
        STA ZP_A
        LDA ZP_SRC+1
        STA ZP_A+1
        JSR PARSE_STR
        BCC PRINT_EXPR
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'='
        BEQ PRINT_REWIND
        CMP #'<'
        BEQ PRINT_REWIND
        CMP #'>'
        BEQ PRINT_REWIND
        JSR PRINT_STRVAL
        LDA #0
        STA ZP_FLAG
        BRA PRINT_LOOP
PRINT_REWIND:
        LDA ZP_A                ; the string was the left side of a comparison
        STA ZP_SRC
        LDA ZP_A+1
        STA ZP_SRC+1            ; the bank byte cannot have moved
PRINT_EXPR:
        JSR EXPR
        LDA ZP_BAD
        BNE PRINT_DONE          ; the expression has already said what it made of it
        LDA ZP_SRC
        CMP ZP_A
        BNE PRINT_ITEM
        LDA ZP_SRC+1
        CMP ZP_A+1
        BNE PRINT_ITEM
        JSR SAY_WHAT            ; nothing was understood: name it and stop
        RTS
PRINT_ITEM:
        JSR SKIPSP              ; an item is only worth printing if what follows
        LDA [ZP_SRC]            ; it could follow it: `PRINT A$` prints nothing
        BEQ PRINT_VALUE
        CMP #';'
        BEQ PRINT_VALUE
        CMP #','
        BEQ PRINT_VALUE
        CMP #':'                ; a colon ends the statement, not the item
        BEQ PRINT_VALUE
        JSR SAY_WHAT
        RTS
PRINT_VALUE:
        JSR PRNUM
        LDA #0                  ; the value ended the item, so the next thing
        STA ZP_FLAG             ; decides whether the line ends
        BRA PRINT_LOOP
PRINT_STRING:
        JSR SRC_STEP
PRINT_STR_LOOP:
        LDA [ZP_SRC]
        BEQ PRINT_END
        CMP #'"'
        BEQ PRINT_STR_CLOSE
        JSL L_CHROUT
        JSR SRC_STEP
        BRA PRINT_STR_LOOP
PRINT_STR_CLOSE:
        JSR SRC_STEP
        LDA #0
        STA ZP_FLAG
        BRA PRINT_LOOP

; PRINT_STRVAL: the string in ZP_SSTR, ZP_SLEN characters of it, goes to the
; tube. The KERNAL's PRINT takes the pointer in ZP_PTR and the length in X, and
; it keeps its own index, so a string that scrolls the screen is still printed
; whole.
PRINT_STRVAL:
        REP #$20
        LDA ZP_SSTR
        STA ZP_PTR
        SEP #$20
        LDA ZP_SSTR+2
        STA ZP_PTR+2
        REP #$20
        LDX ZP_SLEN
        JSL L_PRINT
        SEP #$20
        RTS

; ---------------------------------------------------------------------------
; Strings. A string variable is a fixed slot in bank three -- a length byte and
; STR_MAX characters -- and its address comes from its letter alone, so a name
; always means the same place, an assignment overwrites what was there, and
; nothing ever has to be collected.
; ---------------------------------------------------------------------------

; PARSE_STR: the string at ZP_SRC goes into ZP_SSTR and ZP_SLEN, and the line
; pointer steps past it. Carry clear, and the pointer exactly where it was,
; when what is there is not a string at all.
PARSE_STR:
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'"'
        BEQ PARSE_STR_LITERAL
        LDA ZP_SRC
        STA ZP_SAVE
        LDA ZP_SRC+1
        STA ZP_SAVE+1
        LDA ZP_SRC+2
        STA ZP_SAVE+2
        JSR IS_ALPHA
        BCC PARSE_STR_NONE
        LDA [ZP_SRC]
        JSR TO_UPPER
        SEC
        SBC #'A'
        STA ZP_CH
        JSR SRC_STEP
        LDA [ZP_SRC]
        CMP #'$'
        BEQ PARSE_STR_VAR
        LDA ZP_SAVE             ; a letter on its own is a number: put it back
        STA ZP_SRC
        LDA ZP_SAVE+1
        STA ZP_SRC+1
        LDA ZP_SAVE+2
        STA ZP_SRC+2
PARSE_STR_NONE:
        CLC
        RTS
PARSE_STR_VAR:
        JSR SRC_STEP            ; past the dollar
        JSR STR_ADDR
        SEC
        RTS
PARSE_STR_LITERAL:
        JSR SRC_STEP            ; past the opening quote
        LDA ZP_SRC
        STA ZP_SSTR
        LDA ZP_SRC+1
        STA ZP_SSTR+1
        LDA ZP_SRC+2
        STA ZP_SSTR+2
        REP #$20
        LDA.W #0
        STA ZP_SLEN
        SEP #$20
PARSE_STR_LIT_LOOP:
        LDA [ZP_SRC]
        BEQ PARSE_STR_LIT_DONE  ; a quote that never closes ends its line
        CMP #'"'
        BEQ PARSE_STR_LIT_CLOSE
        JSR SRC_STEP
        REP #$20
        INC ZP_SLEN
        SEP #$20
        BRA PARSE_STR_LIT_LOOP
PARSE_STR_LIT_CLOSE:
        JSR SRC_STEP            ; past the closing quote
PARSE_STR_LIT_DONE:
        SEC
        RTS

; STR_ADDR: ZP_CH = a letter's number. ZP_SSTR = the text of that string, and
; ZP_SLEN = its length.
STR_ADDR:
        REP #$20
        LDA ZP_CH
        AND.W #$00FF
        ASL A
        ASL A
        ASL A
        ASL A
        ASL A                   ; thirty-two bytes a slot
        TAX
        SEP #$20
        LDA.L STRINGS,X         ; the length byte at the slot's start
        STA ZP_SLEN
        LDA #0
        STA ZP_SLEN+1
        REP #$20
        TXA
        INC A                   ; the text begins after the length
        CLC
        ADC.W #(STRINGS & $FFFF)
        STA ZP_SSTR
        SEP #$20
        LDA #BANK_STR
        STA ZP_SSTR+2
        RTS

; STR_STORE: the string in ZP_SSTR goes into the slot for the variable in
; ZP_VAR. A string that will not fit is refused rather than cut.
STR_STORE:
        REP #$10
        REP #$20
        LDA ZP_VAR
        AND.W #$00FF
        ASL A
        ASL A
        ASL A
        ASL A
        ASL A
        CLC
        ADC.W #(STRINGS & $FFFF)
        STA ZP_SDST             ; the slot's length byte
        SEP #$20
        LDA #BANK_STR
        STA ZP_SDST+2
        REP #$20
        LDA ZP_SLEN
        CMP.W #(STR_MAX+1)
        SEP #$20
        BCS STR_STORE_LONG
        LDA ZP_SLEN
        STA [ZP_SDST]
        REP #$20
        LDA ZP_SDST
        INC A                   ; past the length: the text
        STA ZP_SDST
        SEP #$20
        LDY.W #0
STR_STORE_LOOP:
        CPY ZP_SLEN
        BCS STR_STORE_DONE
        LDA [ZP_SSTR],Y
        STA [ZP_SDST],Y
        INY
        BRA STR_STORE_LOOP
STR_STORE_DONE:
        RTS
STR_STORE_LONG:
        JSR SAY_TOOLONG
        RTS

; ---------------------------------------------------------------------------
; Arrays. One dimension, and a DIM before use: twenty-six runs of words in bank
; three, described by a table beside the strings. The heap only ever grows, so a
; DIM that asks for the same size again keeps the run it already has.
; ---------------------------------------------------------------------------

; ARR_ENTRY: ZP_ARR = a letter's number. X = that letter's entry in the table.
; The letter has a byte of its own because the expressions of a DIM and a
; subscript use every other scratch there is: ZP_CH is the digit of a number and
; the letter of a variable, and it is gone by the time the table is wanted.
;
; Its callers are on both sides of a REP: the arithmetic of a DIM is sixteen
; bits wide, so the width it was entered with has to be the width it leaves --
; an eight-bit return from here turned the `CMP.W` after it into two bytes and a
; stray opcode, which the processor took as a reset.
ARR_ENTRY:
        PHP
        REP #$20
        LDA ZP_ARR
        AND.W #$00FF
        ASL A
        ASL A                   ; four bytes an entry
        TAX
        PLP                     ; the caller's widths, and its flags, come back
        RTS

; ARR_ELEM: X = a subscript, ZP_ARR = the letter. ZP_SDST = that element in bank
; three. Carry set, and nothing written, when the letter has no array or the
; subscript is outside it.
ARR_ELEM:
        REP #$20
        TXA
        BMI ARR_ELEM_BAD        ; below nought
        STA ZP_A                ; the subscript
        LDA ZP_ARR
        AND.W #$00FF
        ASL A
        ASL A
        TAX                     ; the entry, in X: there is no long,Y to index by
        LDA.L ARRAYS+2,X        ; how many elements the letter's array has
        BEQ ARR_ELEM_BAD        ; nought: DIM never made one
        CMP ZP_A
        BEQ ARR_ELEM_BAD        ; the end is one past the last element
        BCC ARR_ELEM_BAD
        LDA.L ARRAYS,X          ; where the run starts
        STA ZP_B
        LDA ZP_A
        ASL A                   ; two bytes an element
        CLC
        ADC ZP_B
        STA ZP_SDST             ; the element's offset in bank three
        SEP #$20
        LDA #BANK_STR
        STA ZP_SDST+2
        CLC
        RTS
ARR_ELEM_BAD:
        SEP #$20
        SEC
        RTS

; DO_DIM: `DIM A(10)` gives A eleven elements, nought to ten, which is what the
; 1982 machine's automatic eleven meant. One dimension, one letter, one line.
DO_DIM:
        JSR SKIPSP
        JSR IS_ALPHA
        BCC DIM_BAD
        LDA [ZP_SRC]
        JSR TO_UPPER
        SEC
        SBC #'A'
        STA ZP_VAR              ; kept there: the bound is an expression
        STA ZP_ARR
        JSR SRC_STEP
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'('
        BNE DIM_BAD
        JSR SRC_STEP
        JSR EXPR
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE DIM_BAD
        JSR SRC_STEP
        REP #$20
        TXA
        BMI DIM_BAD_WIDE        ; a negative bound is not a size
        CMP.W #ARR_MAX
        BCS DIM_BAD_WIDE
        INC A
        STA ZP_CNT              ; the elements, nought included
        LDA ZP_VAR
        STA ZP_ARR              ; the bound may have been an array element itself
        JSR ARR_ENTRY
        ; a run of the same size or less is already the letter's, and stays so
        LDA.L ARRAYS,X
        BEQ DIM_TAKE            ; never dimensioned: it needs a run
        LDA.L ARRAYS+2,X        ; how many it has now
        CMP ZP_CNT
        BCC DIM_TAKE            ; smaller than the new one: a new run
        LDA ZP_CNT
        STA.L ARRAYS+2,X        ; as many as were asked for, same place
        SEP #$20
        RTS
DIM_TAKE:
        LDA.L ARR_TOP           ; where the heap is
        STA ZP_A
        LDA ZP_CNT
        ASL A                   ; two bytes an element
        CLC
        ADC ZP_A
        CMP.W #ARR_LIMIT
        BCS DIM_NOROOM
        STA.L ARR_TOP           ; the heap has grown
        LDA ZP_VAR
        STA ZP_ARR
        JSR ARR_ENTRY
        LDA ZP_A
        STA.L ARRAYS,X          ; where the run starts
        LDA ZP_CNT
        STA.L ARRAYS+2,X        ; and how long it is
        SEP #$20
        RTS
DIM_NOROOM:
        SEP #$20
        JSR SAY_NOROOM
        LDA #3
        STA ZP_JFLAG
        RTS
DIM_BAD_WIDE:
        SEP #$20
DIM_BAD:
        JSR SAY_SUBSCRIPT
        LDA #3
        STA ZP_JFLAG
        RTS

PRINT_SEMI:
        JSR SRC_STEP
        LDA #1
        STA ZP_FLAG
        BRA PRINT_LOOP
PRINT_COMMA:
        JSR SRC_STEP
        LDA #32
        JSL L_CHROUT
        LDA #1                  ; a comma continues the line: `PRINT 1,` waits
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

; Levels, loosest first: OR, AND, NOT, one comparison, then the arithmetic the
; language began with. A comparison does not chain -- `1<2<3` is not a question
; this machine answers -- which is what lets its value be returned with the
; machine stack exactly as it was found.
EXPR:
        JSR EXPR_OR
        RTS

EXPR_OR:
        JSR EXPR_AND
        PHX
EXPR_OR_LOOP:
        JSR SKIPSP
        LDX.W #(KW_OR & $FFFF)
        LDY.W #KW_OR_END - KW_OR
        JSR MATCHKW
        BCC EXPR_OR_DONE
        JSR EXPR_AND
        PLY
        REP #$20
        TXA
        BNE EXPR_OR_TRUE
        TYA
        BNE EXPR_OR_TRUE
        LDA.W #0
        BRA EXPR_OR_PUSH
EXPR_OR_TRUE:
        LDA.W #$FFFF
EXPR_OR_PUSH:
        TAX
        SEP #$20
        PHX
        BRA EXPR_OR_LOOP
EXPR_OR_DONE:
        PLX
        RTS

EXPR_AND:
        JSR EXPR_NOT
        PHX
EXPR_AND_LOOP:
        JSR SKIPSP
        LDX.W #(KW_AND & $FFFF)
        LDY.W #KW_AND_END - KW_AND
        JSR MATCHKW
        BCC EXPR_AND_DONE
        JSR EXPR_NOT
        PLY
        REP #$20
        TXA
        BEQ EXPR_AND_FALSE
        TYA
        BEQ EXPR_AND_FALSE
        LDA.W #$FFFF
        BRA EXPR_AND_PUSH
EXPR_AND_FALSE:
        LDA.W #0
EXPR_AND_PUSH:
        TAX
        SEP #$20
        PHX
        BRA EXPR_AND_LOOP
EXPR_AND_DONE:
        PLX
        RTS

EXPR_NOT:
        JSR SKIPSP
        LDX.W #(KW_NOT & $FFFF)
        LDY.W #KW_NOT_END - KW_NOT
        JSR MATCHKW
        BCC EXPR_CMP
        JSR EXPR_NOT
        REP #$20
        TXA
        BNE EXPR_NOT_SAIDNO
        LDA.W #$FFFF
        BRA EXPR_NOT_PUSH
EXPR_NOT_SAIDNO:
        LDA.W #0
EXPR_NOT_PUSH:
        TAX
        SEP #$20
        RTS

EXPR_CMP:
        ; A string on the left makes this a string comparison: the two sides are
        ; compared a character at a time and the answer is -1 or 0, like the
        ; numbers, so `IF A$="YES" THEN` branches on it.
        JSR PARSE_STR
        BCC EXPR_CMP_NUM
        JSR STR_SAVE
        JSR EXPR_CMP_OP
        BCC EXPR_CMP_STRBAD     ; a string on its own is not a value
        JSR PARSE_STR
        BCC EXPR_CMP_STRBAD     ; and a string cannot be compared with a number
        JSR STR_COMPARE
        BRA EXPR_CMP_APPLY
EXPR_CMP_STRBAD:
        JMP FACTOR_MISMATCH

; EXPR_CMP_OP: the comparison operator at ZP_SRC goes into ZP_CNT2 as one of
; six, and the pointer steps past it. Carry clear, and the pointer alone, when
; there is no operator: `PRINT 1+2` is not a comparison.
EXPR_CMP_OP:
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'='
        BEQ EXPR_CMP_OP_EQ
        CMP #'<'
        BEQ EXPR_CMP_OP_LT
        CMP #'>'
        BEQ EXPR_CMP_OP_GT
        CLC
        RTS
EXPR_CMP_OP_EQ:
        JSR SRC_STEP
        LDA #1                  ; =
        BRA EXPR_CMP_OP_HAVE
EXPR_CMP_OP_LT:
        JSR SRC_STEP
        LDA [ZP_SRC]
        CMP #'='
        BEQ EXPR_CMP_OP_LE
        CMP #'>'
        BEQ EXPR_CMP_OP_NE
        LDA #3                  ; <
        BRA EXPR_CMP_OP_HAVE
EXPR_CMP_OP_LE:
        JSR SRC_STEP
        LDA #5                  ; <=
        BRA EXPR_CMP_OP_HAVE
EXPR_CMP_OP_NE:
        JSR SRC_STEP
        LDA #2                  ; <>
        BRA EXPR_CMP_OP_HAVE
EXPR_CMP_OP_GT:
        JSR SRC_STEP
        LDA [ZP_SRC]
        CMP #'='
        BEQ EXPR_CMP_OP_GE
        LDA #4                  ; >
        BRA EXPR_CMP_OP_HAVE
EXPR_CMP_OP_GE:
        JSR SRC_STEP
        LDA #6                  ; >=
EXPR_CMP_OP_HAVE:
        STA ZP_CNT2
        SEC
        RTS

; STR_SAVE: the string being held goes into the second pair, so the right-hand
; side of a comparison can use the first.
STR_SAVE:
        LDA ZP_SSTR
        STA ZP_SSTR2
        LDA ZP_SSTR+1
        STA ZP_SSTR2+1
        LDA ZP_SSTR+2
        STA ZP_SSTR2+2
        REP #$20
        LDA ZP_SLEN
        STA ZP_SLEN2
        SEP #$20
        RTS

; STR_COMPARE: the string in ZP_SSTR2/ZP_SLEN2 against the one in
; ZP_SSTR/ZP_SLEN, a character at a time. ZP_TMP16 comes back 0 for the same
; text, 1 when the first is lower, 2 when it is higher; a string that ends first
; is the lower, which is how `"AB" < "ABC"` and every dictionary agrees.
STR_COMPARE:
        REP #$10
        LDY.W #0
STR_COMPARE_LOOP:
        CPY ZP_SLEN2
        BEQ STR_COMPARE_LEFTEND
        CPY ZP_SLEN
        BEQ STR_COMPARE_HIGHER  ; the left has a character the right has not
        LDA [ZP_SSTR2],Y
        STA ZP_CH
        LDA [ZP_SSTR],Y
        CMP ZP_CH
        BCC STR_COMPARE_HIGHER  ; the right is lower, so the left is higher
        BNE STR_COMPARE_LOWER   ; the right is higher, so the left is lower
        INY
        BRA STR_COMPARE_LOOP
STR_COMPARE_LEFTEND:
        CPY ZP_SLEN
        BEQ STR_COMPARE_SAME
        BRA STR_COMPARE_LOWER   ; the left ended first, so it is the lower
STR_COMPARE_SAME:
        REP #$20
        LDA.W #0
        BRA STR_COMPARE_SET
STR_COMPARE_LOWER:
        REP #$20
        LDA.W #1
        BRA STR_COMPARE_SET
STR_COMPARE_HIGHER:
        REP #$20
        LDA.W #2
STR_COMPARE_SET:
        STA ZP_TMP16
        SEP #$20
        RTS
EXPR_CMP_NUM:
        JSR EXPR_ARITH
        PHX                     ; the left side, until an operator is found
        JSR EXPR_CMP_OP
        BCC EXPR_CMP_NONE
        JSR EXPR_ARITH
        PLY                     ; the left side comes back
        REP #$20
        TYA
        EOR.W #$8000
        STA ZP_TMP16            ; left, biased into unsigned order
        TXA
        EOR.W #$8000
        CMP ZP_TMP16
        BEQ EXPR_CMP_SAME
        BCC EXPR_CMP_ABOVE      ; the right is lower, so the left is greater
        LDA.W #1                ; left < right
        BRA EXPR_CMP_HAVE
EXPR_CMP_ABOVE:
        LDA.W #2
        BRA EXPR_CMP_HAVE
EXPR_CMP_SAME:
        LDA.W #0
EXPR_CMP_HAVE:
        STA ZP_TMP16            ; 0 equal, 1 left is less, 2 left is greater
EXPR_CMP_APPLY:
        SEP #$20
        LDA ZP_CNT2
        CMP #1
        BEQ EXPR_CMP_WANT_SAME
        CMP #2
        BEQ EXPR_CMP_WANT_DIFF
        CMP #3
        BEQ EXPR_CMP_WANT_LESS
        CMP #4
        BEQ EXPR_CMP_WANT_GREATER
        CMP #5
        BEQ EXPR_CMP_WANT_NOTGREATER
        LDA ZP_TMP16            ; >=
        CMP #1
        BEQ EXPR_CMP_SAYNO
        BRA EXPR_CMP_SAYYES
EXPR_CMP_WANT_SAME:
        LDA ZP_TMP16
        BEQ EXPR_CMP_SAYYES
        BRA EXPR_CMP_SAYNO
EXPR_CMP_WANT_DIFF:
        LDA ZP_TMP16
        BNE EXPR_CMP_SAYYES
        BRA EXPR_CMP_SAYNO
EXPR_CMP_WANT_LESS:
        LDA ZP_TMP16
        CMP #1
        BEQ EXPR_CMP_SAYYES
        BRA EXPR_CMP_SAYNO
EXPR_CMP_WANT_GREATER:
        LDA ZP_TMP16
        CMP #2
        BEQ EXPR_CMP_SAYYES
        BRA EXPR_CMP_SAYNO
EXPR_CMP_WANT_NOTGREATER:
        LDA ZP_TMP16
        CMP #2
        BEQ EXPR_CMP_SAYNO
EXPR_CMP_SAYYES:
        REP #$20
        LDA.W #$FFFF
        TAX
        SEP #$20
        RTS
EXPR_CMP_SAYNO:
        REP #$20
        LDA.W #0
        TAX
        SEP #$20
        RTS
EXPR_CMP_NONE:
        PLX                     ; not a comparison: the value itself
        RTS

; The arithmetic the language began with.
EXPR_ARITH:
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
        JSR SRC_STEP
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
        JSR SRC_STEP
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
        JSR SRC_STEP
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
        JSR SRC_STEP
        JSR FACTOR
        PLY
        REP #$20
        TXA
        STA ZP_B                ; the divisor
        TYA
        STA ZP_A                ; the dividend
        ; UDIV16 divides magnitudes, so the signs come off first and go back on
        ; afterwards: a negative dividend or divisor would otherwise be read as a
        ; number near $FFxx, and -8/2 would come back as 32764.
        LDA.W #0
        STA ZP_SGN
        LDA ZP_A
        BPL DIV_A_POSITIVE
        LDA.W #0
        SEC
        SBC ZP_A
        STA ZP_A
        INC ZP_SGN
DIV_A_POSITIVE:
        LDA ZP_B
        BPL DIV_B_POSITIVE
        LDA.W #0
        SEC
        SBC ZP_B
        STA ZP_B
        INC ZP_SGN
DIV_B_POSITIVE:
        LDA ZP_B
        BEQ DIV_BY_ZERO
        JSR UDIV16              ; comes back with an eight-bit accumulator
        REP #$20
        LDA ZP_SGN
        AND.W #1                ; a word immediate: the accumulator is wide here
        BEQ DIV_QUOTIENT
        LDA.W #0
        SEC
        SBC ZP_A
        STA ZP_A
DIV_QUOTIENT:
        LDA ZP_A
        TAX
        SEP #$20
        PHX
        BRA TERM_LOOP
DIV_BY_ZERO:
        ; The machine has one way of saying a value is not one: ZP_BAD, which
        ; PRINT reads before it prints anything, so the line stops here.
        SEP #$20
        LDA #1
        STA ZP_BAD
        JSR SAY_WHAT
        REP #$20
        LDA.W #0
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
        CMP #'"'
        BEQ FACTOR_ISSTR        ; a string where a number was wanted
        CMP #'0'
        BCC FACTOR_WORD
        CMP #'9'+1
        BCS FACTOR_WORD
        BRA FACTOR_NUM
FACTOR_WORD:
        ; LEN(<string>) is the one function this language has, and it is worth
        ; having: a string knows how long it is, and nothing else can ask.
        LDX.W #(KW_LEN & $FFFF)
        LDY.W #KW_LEN_END - KW_LEN
        JSR MATCHKW
        BCS FACTOR_LEN
        BRA FACTOR_VAR
FACTOR_ISSTR:
        JMP FACTOR_MISMATCH
FACTOR_LEN:
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'('
        BNE FACTOR_MISMATCH
        JSR SRC_STEP
        JSR PARSE_STR
        BCC FACTOR_MISMATCH
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE FACTOR_MISMATCH
        JSR SRC_STEP
        REP #$20
        LDA ZP_SLEN
        TAX
        SEP #$20
        RTS
FACTOR_MISMATCH:
        JSR SAY_MISMATCH
        LDA #1
        STA ZP_BAD              ; the statement has been named as unreadable
        LDA #3                  ; and the line stops here, as it does on ?WHAT?
        STA ZP_JFLAG
        REP #$20
        LDA #0
        TAX
        SEP #$20
        RTS
FACTOR_PLUS:
        JSR SRC_STEP
        BRA FACTOR
FACTOR_PAREN:
        JSR SRC_STEP
        JSR EXPR
        PHX
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE FACTOR_PAREN_DONE
        JSR SRC_STEP
FACTOR_PAREN_DONE:
        PLX
        RTS
FACTOR_NEG:
        JSR SRC_STEP
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
        JSR SRC_STEP
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
        JSR SRC_STEP
        LDA [ZP_SRC]
        CMP #'('
        BEQ FACTOR_ARRAY        ; `A(3)` is an element of an array
        CMP #'$'
        BEQ FACTOR_MISMATCH     ; `A$` is a string where a number was wanted
        LDA ZP_CH
        REP #$20
        AND.W #$00FF
        ASL A
        TAX
        LDA VARS,X
        TAX
        SEP #$20
        RTS
FACTOR_ARRAY:
        LDA ZP_CH               ; the letter, which the subscript cannot take
        STA ZP_ARR
        JSR SRC_STEP            ; past the opening bracket
        JSR EXPR                ; X = the subscript
        PHX
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE FACTOR_ARRAY_BAD
        JSR SRC_STEP
        PLX
        JSR ARR_ELEM
        BCS FACTOR_ARRAY_BAD
        REP #$20
        LDA [ZP_SDST]           ; a word from bank three
        TAX
        SEP #$20
        RTS
FACTOR_ARRAY_BAD:
        JSR SAY_SUBSCRIPT
        LDA #1
        STA ZP_BAD
        LDA #3
        STA ZP_JFLAG
        REP #$20
        LDA.W #0
        TAX
        SEP #$20
        RTS
FACTOR_NONE:
        LDA #1
        STA ZP_BAD              ; the statement has been named as unreadable
        JSR SAY_WHAT
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

        .include "prog.inc"
