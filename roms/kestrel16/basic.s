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
        INC ZP_SRC
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
        INC ZP_SRC
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
        INC ZP_SRC
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
        INC ZP_SRC
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
        INC ZP_SRC
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
        INC ZP_SRC
        BRA PARSEFN_LOOP
PARSEFN_CLOSE:
        INC ZP_SRC
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
        STA ZP_BAD
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
        ; An item has to consume something: `PRINT A$` used to fall through
        ; here for ever, printing a zero a time, because `$` is neither a
        ; separator nor the start of an expression.
        LDA ZP_SRC              ; where the item starts, in a scratch operand
        STA ZP_A
        LDA ZP_SRC+1
        STA ZP_A+1
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
        JSR SAY_WHAT
        RTS
PRINT_VALUE:
        JSR PRNUM
        LDA #0                  ; the value ended the item, so the next thing
        STA ZP_FLAG             ; decides whether the line ends
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
        LDA #0
        STA ZP_FLAG
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
        JSR EXPR_ARITH
        PHX                     ; the left side, until an operator is found
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'='
        BEQ EXPR_CMP_EQ
        CMP #'<'
        BEQ EXPR_CMP_LT
        CMP #'>'
        BEQ EXPR_CMP_GT
        PLX                     ; not a comparison: the value itself
        RTS
EXPR_CMP_EQ:
        INC ZP_SRC
        LDA #1
        BRA EXPR_CMP_GO
EXPR_CMP_LT:
        INC ZP_SRC
        LDA [ZP_SRC]
        CMP #'='
        BEQ EXPR_CMP_LE
        CMP #'>'
        BEQ EXPR_CMP_NE
        LDA #3
        BRA EXPR_CMP_GO
EXPR_CMP_LE:
        INC ZP_SRC
        LDA #5
        BRA EXPR_CMP_GO
EXPR_CMP_NE:
        INC ZP_SRC
        LDA #2
        BRA EXPR_CMP_GO
EXPR_CMP_GT:
        INC ZP_SRC
        LDA [ZP_SRC]
        CMP #'='
        BEQ EXPR_CMP_GE
        LDA #4
        BRA EXPR_CMP_GO
EXPR_CMP_GE:
        INC ZP_SRC
        LDA #6
EXPR_CMP_GO:
        STA ZP_CNT2             ; the operator
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
