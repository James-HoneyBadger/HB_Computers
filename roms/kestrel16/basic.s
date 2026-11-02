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
ZP_STOT         = $0E           ; 16-bit: how long the joined string is so far
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
KW_FIND:
        .text "FIND"
KW_FIND_END:
KW_CSAVE:
        .text "CSAVE"
KW_CSAVE_END:
KW_CLOAD:
        .text "CLOAD"
KW_CLOAD_END:
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
KW_ABS:
        .text "ABS"
KW_ABS_END:
KW_SGN:
        .text "SGN"
KW_SGN_END:
KW_DIM:
        .text "DIM"
KW_DIM_END:
KW_DEF:
        .text "DEF"
KW_DEF_END:
KW_FN:
        .text "FN"
KW_FN_END:
KW_LEFT:
        .text "LEFT$"
KW_LEFT_END:
KW_RIGHT:
        .text "RIGHT$"
KW_RIGHT_END:
KW_MID:
        .text "MID$"
KW_MID_END:
KW_CHR:
        .text "CHR$"
KW_CHR_END:
KW_ASC:
        .text "ASC"
KW_ASC_END:
KW_STR:
        .text "STR$"
KW_STR_END:
KW_VAL:
        .text "VAL"
KW_VAL_END:
KW_PEEK:
        .text "PEEK"
KW_PEEK_END:
KW_POKE:
        .text "POKE"
KW_POKE_END:
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
TXT_NOTFOUND:
        .text "?NOT FOUND"
TXT_NOTFOUND_END:
TXT_DISKERR:
        .text "?DISK"
TXT_DISKERR_END:

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
SAY_NOTFOUND:
        LDA #<TXT_NOTFOUND
        STA ZP_PTR
        LDA #>TXT_NOTFOUND
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_NOTFOUND_END - TXT_NOTFOUND
        JSL L_PRINT
        JSR SAY_NEWLINE
        RTS
SAY_DISKERR:
        LDA #<TXT_DISKERR
        STA ZP_PTR
        LDA #>TXT_DISKERR
        STA ZP_PTR+1
        LDA #BANK_BASIC
        STA ZP_PTR+2
        LDX.W #TXT_DISKERR_END - TXT_DISKERR
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
        LDA #3                  ; an error ends the line, and the program with
        STA ZP_JFLAG            ; it: the rest of it is not read, and the next
        RTS                     ; statement is not run

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
        LDA #0                  ; what the last statement could not read is not
        STA ZP_BAD              ; this one's business
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
        LDX.W #(KW_CSAVE & $FFFF)
        LDY.W #KW_CSAVE_END - KW_CSAVE
        JSR MATCHKW
        BCS DO_CSAVE
        LDX.W #(KW_CLOAD & $FFFF)
        LDY.W #KW_CLOAD_END - KW_CLOAD
        JSR MATCHKW
        BCS DO_CLOAD
        LDX.W #(KW_FIND & $FFFF)
        LDY.W #KW_FIND_END - KW_FIND
        JSR MATCHKW
        BCS DO_FIND
        LDX.W #(KW_DELETE & $FFFF)
        LDY.W #KW_DELETE_END - KW_DELETE
        JSR MATCHKW
        BCS DO_DELETE
        LDX.W #(KW_DEF & $FFFF)
        LDY.W #KW_DEF_END - KW_DEF
        JSR MATCHKW
        BCS DO_DEF
        LDX.W #(KW_REM & $FFFF)
        LDY.W #KW_REM_END - KW_REM
        JSR MATCHKW
        BCS DO_REM
        LDX.W #(KW_POKE & $FFFF)
        LDY.W #KW_POKE_END - KW_POKE
        JSR MATCHKW
        BCS DO_POKE
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

; DO_CSAVE: the program store goes onto the cassette as a named block.
DO_CSAVE:
        JSR PARSEFN
        BCS CSAVE_NONAME
        REP #$20
        LDA.W #DOSNAME
        SEP #$20
        JSL L_TAPESAVE
        CMP #0
        BNE EXEC_DONE
        JSR SAY_SAVED
        BRA EXEC_DONE
CSAVE_NONAME:
        JSR SAY_NONAME
        BRA EXEC_DONE

; DO_CLOAD: a named block on the cassette becomes the program store. The program
; that was running is not there any more, so the line stops, as it does on LOAD.
DO_CLOAD:
        JSR PARSEFN
        BCS CLOAD_NONAME
        REP #$20
        LDA.W #DOSNAME
        SEP #$20
        JSL L_TAPELOAD
        CMP #0
        BNE EXEC_DONE
        LDA #3
        STA ZP_JFLAG
        JSR RESET_STACKS
        JSR SAY_LOADED
        BRA EXEC_DONE
CLOAD_NONAME:
        JSR SAY_NONAME
        BRA EXEC_DONE

; DO_DEF: `DEF FNA(X)=X*X+1` gives the letter a function of one argument. The
; text after the `=` is copied into the letter's own slot in bank three, so it
; outlives the line it was typed on -- at the prompt, or in a program that is
; then edited. A definition runs to the end of its line.
DO_DEF:
        JSR SKIPSP
        LDX.W #(KW_FN & $FFFF)
        LDY.W #KW_FN_END - KW_FN
        JSR MATCHKW
        BCC DEF_BAD
        JSR SKIPSP
        JSR IS_ALPHA
        BCC DEF_BAD
        LDA [ZP_SRC]
        JSR TO_UPPER
        SEC
        SBC #'A'
        STA ZP_CH               ; the function's letter
        JSR SRC_STEP
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'('
        BNE DEF_BAD
        JSR SRC_STEP
        JSR SKIPSP
        JSR IS_ALPHA
        BCC DEF_BAD
        LDA [ZP_SRC]
        JSR TO_UPPER
        SEC
        SBC #'A'
        STA ZP_VAR              ; and the letter its argument is called by
        JSR SRC_STEP
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE DEF_BAD
        JSR SRC_STEP
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'='
        BNE DEF_BAD
        JSR SRC_STEP
        JSR SKIPSP
        LDA [ZP_SRC]
        BEQ DEF_BAD             ; a definition with nothing in it is not one
        JSR DEF_TEXTADDR        ; ZP_SDST = the letter's own slot
        REP #$10
        LDY.W #0
DEF_COPY:
        LDA [ZP_SRC]
        BEQ DEF_COPY_END        ; the line's end, or a colon, ends the text
        CMP #':'
        BEQ DEF_COPY_END
        CPY.W #(DEF_SIZE-1)
        BCS DEF_BAD             ; more than a slot holds
        STA [ZP_SDST],Y
        JSR SRC_STEP
        INY
        BRA DEF_COPY
DEF_COPY_END:
        LDA #0
        STA [ZP_SDST],Y         ; the text ends with a zero, as a line does
        ; the entry says where the text is and what the argument is called
        JSR DEF_ENTRYADDR       ; X = the entry
        REP #$20
        LDA ZP_SDST
        STA.L DEFNS,X
        SEP #$20
        LDA ZP_SDST+2
        STA.L DEFNS+2,X         ; the bank of it, which is what says it is there
        LDA ZP_VAR
        STA.L DEFNS+3,X
        JMP EXEC_DONE
DEF_BAD:
        JSR SAY_WHAT
        JMP DO_REM

; DEF_ENTRYADDR: ZP_CH = a function's letter. X = its entry in the table.
DEF_ENTRYADDR:
        PHP
        REP #$20
        LDA ZP_CH
        AND.W #$00FF
        ASL A
        ASL A                   ; four bytes an entry
        TAX
        PLP
        RTS

; DEF_ENTRYADDR_SEL: X = the entry of the function named in DEFNSEL. Unlike
; DEF_ENTRYADDR it reads nothing from zero page, which an expression has been
; through since the letter was read.
DEF_ENTRYADDR_SEL:
        PHP
        REP #$20
        LDA.L DEFNSEL
        AND.W #$00FF
        ASL A
        ASL A                   ; four bytes an entry
        TAX
        PLP
        RTS

; DEF_TEXTADDR: ZP_CH = a function's letter. ZP_SDST = its text in bank three.
DEF_TEXTADDR:
        PHP
        REP #$20
        LDA ZP_CH
        AND.W #$00FF
        ASL A
        ASL A
        ASL A
        ASL A
        ASL A
        ASL A                   ; DEF_SIZE bytes a definition
        CLC
        ADC.W #(DEFTEXT & $FFFF)
        STA ZP_SDST
        SEP #$20
        LDA #BANK_STR
        STA ZP_SDST+2
        PLP
        RTS

; DO_FIND: `FIND "NAME"` asks the filesystem where a name is and prints what it
; says: the directory entry, the sector the file starts at, and how many sectors
; it runs for. A program has to be told these, because the language has no way
; of keeping a number the filesystem handed back.
DO_FIND:
        JSR PARSEFN
        BCS FIND_NONAME
        REP #$20
        LDA.W #DOSNAME
        SEP #$20
        JSL L_DOSFIND
        CMP #0
        BEQ FIND_FOUND
        CMP #1                  ; nought for a name that is there, one for a
        BEQ FIND_MISSING        ; name that is not, two for a disk that would not
        JSR SAY_DISKERR
        BRA EXEC_DONE
FIND_MISSING:
        JSR SAY_NOTFOUND
        BRA EXEC_DONE
FIND_FOUND:
        ; Each number is read out of the DOS's own zero page as it is printed:
        ; PRINT's arithmetic uses every byte of the language's.
        REP #$20
        TXA
        AND.W #$00FF
        TAX
        SEP #$20
        JSR PRNUM
        LDA #' '
        JSL L_CHROUT
        REP #$20
        LDA ZP_DOSFIRST
        TAX
        SEP #$20
        JSR PRNUM
        LDA #' '
        JSL L_CHROUT
        REP #$20
        LDA ZP_DOSCOUNT
        TAX
        SEP #$20
        JSR PRNUM
        JSR SAY_NEWLINE
        BRA EXEC_DONE
FIND_NONAME:
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
        JMP DO_REM              ; one error for the line, not one per letter
ASSIGN_STR:
        JSR SRC_STEP            ; past the dollar
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'='
        BEQ ASSIGN_STR_STORE
        CMP #'('                ; `A$(3)="text"` writes one element of one
        BEQ ASSIGN_STR_ELEM
        JSR SAY_WHAT
        JMP DO_REM
ASSIGN_STR_STORE:
        JSR SRC_STEP            ; past the equals
        JSR PARSE_STR
        BCC ASSIGN_MISMATCH     ; a number cannot go into a string
        JSR STR_STORE
        RTS
ASSIGN_STR_ELEM:
        JSR PARSE_ELEM          ; ZP_SDST = the element
        BCS ASSIGN_SUBSCRIPT
        JSR ARR_IS_STRING       ; a word is not a string
        BCC ASSIGN_MISMATCH
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'='
        BEQ ASSIGN_STR_ELEM_STORE
        JSR SAY_WHAT
        RTS
ASSIGN_STR_ELEM_STORE:
        JSR SRC_STEP            ; past the equals
        JSR PARSE_STR
        BCC ASSIGN_MISMATCH
        JSR STR_COPY            ; into the element's slot
        RTS
ASSIGN_MISMATCH:
        JSR SAY_MISMATCH
        LDA #3                  ; the line stops here, as it does on ?WHAT?
        STA ZP_JFLAG
        RTS
ASSIGN_ARRAY:
        JSR PARSE_ELEM          ; ZP_SDST = the element
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

; DO_POKE: `POKE address, value` writes one byte of bank zero. It is the other
; half of PEEK, and the only way a program has of reaching the tube, the colour
; registers or the sound latch.
DO_POKE:
        JSR SKIPSP
        JSR EXPR                ; X = the address
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #','
        BNE DO_POKE_BAD
        JSR SRC_STEP
        PHX                     ; the address waits on the stack: the value's
        JSR EXPR                ; own expression uses every byte of zero page
        REP #$20
        TXA
        TAY                     ; the value
        PLX                     ; and the address, back again
        SEP #$20
        TYA
        STA.L $000000,X         ; one byte of bank zero, and not of zero page
        BRA EXEC_DONE
DO_POKE_BAD:
        JSR SAY_WHAT
        BRA EXEC_DONE

; ---------------------------------------------------------------------------
; Strings. A string variable is a fixed slot in bank three -- a length byte and
; STR_MAX characters -- and its address comes from its letter alone, so a name
; always means the same place, an assignment overwrites what was there, and
; nothing ever has to be collected.
; ---------------------------------------------------------------------------

; PARSE_STR: the string expression at ZP_SRC goes into ZP_SSTR and ZP_SLEN,
; and the line pointer steps past it. Carry clear, and the pointer exactly
; where it was, when what is there is not a string at all.
;
; The expression is factors joined by `+`: a literal, a string variable, or one
; of the four functions that make a string. Two of them are joined in the
; temporary, because a result that is neither one operand nor the other has to
; live somewhere, and the temporary is `STR_MAX` bytes of bank three that
; nothing else wants.
PARSE_STR:
        JSR PARSE_STR_ONE
        BCC PARSE_STR_EMPTY
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'+'
        BNE PARSE_STR_ONE_DONE
        JSR STR_TO_TEMP         ; the left side moves, and the rest is added
PARSE_STR_MORE:
        JSR SRC_STEP            ; past the plus
        JSR PARSE_STR_ONE
        BCC PARSE_STR_BADADD    ; a number where the other half of a sum was
        JSR STR_APPEND
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'+'
        BEQ PARSE_STR_MORE
PARSE_STR_ONE_DONE:
        SEC
        RTS
PARSE_STR_BADADD:
        JMP FACTOR_MISMATCH
PARSE_STR_EMPTY:
        CLC
        RTS

; PARSE_STR_ONE: one string, with no `+` in it.
PARSE_STR_ONE:
        JSR SKIPSP
        ; the functions first: `LEFT$` begins with a letter, and so does `A$`
        LDX.W #(KW_LEFT & $FFFF)
        LDY.W #KW_LEFT_END - KW_LEFT
        JSR MATCHKW
        BCS PARSE_LEFT
        LDX.W #(KW_RIGHT & $FFFF)
        LDY.W #KW_RIGHT_END - KW_RIGHT
        JSR MATCHKW
        BCS PARSE_RIGHT
        LDX.W #(KW_MID & $FFFF)
        LDY.W #KW_MID_END - KW_MID
        JSR MATCHKW
        BCS PARSE_MID
        LDX.W #(KW_CHR & $FFFF)
        LDY.W #KW_CHR_END - KW_CHR
        JSR MATCHKW
        BCS PARSE_CHR
        LDX.W #(KW_STR & $FFFF)
        LDY.W #KW_STR_END - KW_STR
        JSR MATCHKW
        BCS PARSE_STR_NUM
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
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'('                ; `A$(3)` is one element of a string array
        BEQ PARSE_STR_ELEM
        JSR STR_ADDR
        SEC
        RTS
PARSE_STR_ELEM:
        LDA ZP_CH               ; the letter, which the subscripts cannot take
        STA ZP_ARR
        JSR PARSE_ELEM          ; ZP_SDST = the element
        BCS PARSE_STR_ELEM_BAD
        JSR ARR_IS_STRING
        BCC PARSE_STR_ELEM_WORD
        LDA ZP_SDST
        STA ZP_SSTR
        LDA ZP_SDST+1
        STA ZP_SSTR+1
        LDA ZP_SDST+2
        STA ZP_SSTR+2
        LDY.W #0
        LDA [ZP_SSTR],Y         ; the slot's own length byte
        REP #$20
        AND.W #$00FF
        STA ZP_SLEN
        LDA ZP_SSTR
        INC A                   ; and the text follows the length, as everywhere
        STA ZP_SSTR
        SEP #$20
        SEC
        RTS
PARSE_STR_ELEM_WORD:
        JSR SAY_MISMATCH        ; a word is not a string
        BRA PARSE_STR_ELEM_EMPTY
PARSE_STR_ELEM_BAD:
        JSR SAY_SUBSCRIPT
PARSE_STR_ELEM_EMPTY:
        LDA #3
        STA ZP_JFLAG
        REP #$20                ; what comes back is an empty string, so a caller
        LDA.W #0                ; that goes on does not read a slot that is not
        STA ZP_SLEN             ; there
        LDA.W #(STRTMP & $FFFF)
        STA ZP_SSTR
        SEP #$20
        LDA #BANK_STR
        STA ZP_SSTR+2
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

; ---------------------------------------------------------------------------
; The functions that make a string. Each takes its own name already matched, so
; the pointer stands at the bracket.
; ---------------------------------------------------------------------------

; PARSE_SLICE_OPEN: past the bracket, and the string inside it is in
; ZP_SSTR/ZP_SLEN. Carry set when that is not a string.
PARSE_SLICE_OPEN:
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'('
        BNE PARSE_SLICE_BAD
        JSR SRC_STEP
        JSR PARSE_STR
        RTS
PARSE_SLICE_BAD:
        JMP FACTOR_MISMATCH

; PARSE_LEFT: LEFT$(s,n) -- the first n characters, and all of them if n is more
; than there are.
PARSE_LEFT:
        JSR PARSE_SLICE_OPEN
        BCC PARSE_SLICE_BAD
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #','
        BNE PARSE_SLICE_BAD
        JSR SRC_STEP
        JSR EXPR
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE PARSE_SLICE_BAD
        JSR SRC_STEP
        JSR SLICE_CLAMP         ; X = the count, no more than the string has
        REP #$20
        LDA ZP_A
        STA ZP_SLEN
        SEP #$20
        SEC
        RTS

; PARSE_RIGHT: RIGHT$(s,n) -- the last n characters.
PARSE_RIGHT:
        JSR PARSE_SLICE_OPEN
        BCC PARSE_SLICE_BAD
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #','
        BNE PARSE_SLICE_BAD
        JSR SRC_STEP
        JSR EXPR
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE PARSE_SLICE_BAD
        JSR SRC_STEP
        JSR SLICE_CLAMP         ; ZP_A = the count
        REP #$20
        LDA ZP_SLEN
        SEC
        SBC ZP_A                ; this many characters are left out
        CLC
        ADC ZP_SSTR             ; so the text starts there
        STA ZP_SSTR
        LDA ZP_A
        STA ZP_SLEN
        SEP #$20
        SEC
        RTS

; PARSE_MID: MID$(s,start,n) -- n characters from the start, counting the first
; character as one.
PARSE_MID:
        JSR PARSE_SLICE_OPEN
        BCC PARSE_SLICE_BAD
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #','
        BNE PARSE_SLICE_BAD
        JSR SRC_STEP
        JSR EXPR                ; the first character wanted
        REP #$20
        TXA
        STA ZP_B                ; kept: the count's own expression follows
        SEP #$20
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #','
        BNE PARSE_SLICE_BAD
        JSR SRC_STEP
        JSR EXPR
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE PARSE_SLICE_BAD
        JSR SRC_STEP
        JSR SLICE_CLAMP_COUNT   ; ZP_A = the count, no more than could be there
        REP #$20
        LDA ZP_B                ; the start, counting the first character as one
        BPL MID_HAVE_START
        LDA.W #1
MID_HAVE_START:
        DEC A                   ; how many characters come before it
        BPL MID_HAVE_OFFSET
        LDA.W #0
MID_HAVE_OFFSET:
        CMP ZP_SLEN
        BCC MID_HAVE_TEXT
        LDA ZP_SLEN             ; a start past the end is an empty string
MID_HAVE_TEXT:
        STA ZP_B
        LDA ZP_SSTR
        CLC
        ADC ZP_B
        STA ZP_SSTR
        LDA ZP_SLEN
        SEC
        SBC ZP_B                ; how much of the string is left from there
        STA ZP_B
        LDA ZP_A
        CMP ZP_B
        BCC MID_HAVE_COUNT
        LDA ZP_B                ; all that is left is fewer than was asked for
MID_HAVE_COUNT:
        STA ZP_SLEN
        SEP #$20
        SEC
        RTS

; SLICE_CLAMP: X = a count, ZP_SLEN = how many characters there are. ZP_A = the
; count the string can honour: nought for nought or less, and the whole string
; when the count is more than it has. A negative count is nought rather than a
; pointer that walks backwards.
SLICE_CLAMP_COUNT = SLICE_CLAMP
SLICE_CLAMP:
        REP #$20
        TXA
        BPL SLICE_CLAMP_POSITIVE
        LDA.W #0
SLICE_CLAMP_POSITIVE:
        CMP ZP_SLEN
        BCC SLICE_CLAMP_HAVE
        BEQ SLICE_CLAMP_HAVE
        LDA ZP_SLEN
SLICE_CLAMP_HAVE:
        STA ZP_A
        SEP #$20
        RTS

; PARSE_CHR: CHR$(n) -- the character whose number is n, in a byte of its own.
PARSE_CHR:
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'('
        BNE PARSE_SLICE_BAD
        JSR SRC_STEP
        JSR EXPR
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE PARSE_SLICE_BAD
        JSR SRC_STEP
        REP #$20
        TXA
        SEP #$20
        STA.L STRCHR
        REP #$20
        LDA.W #(STRCHR & $FFFF)
        STA ZP_SSTR
        LDA.W #1
        STA ZP_SLEN
        SEP #$20
        LDA #BANK_STR
        STA ZP_SSTR+2
        SEC
        RTS

; PARSE_STR_NUM: STR$(n) -- the digits of a number, in a buffer of their own.
PARSE_STR_NUM:
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'('
        BNE PARSE_SLICE_BAD
        JSR SRC_STEP
        JSR EXPR                ; X = the number
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE PARSE_SLICE_BAD
        JSR SRC_STEP
        JSR STRNUM_FROM         ; writes the digits, X = how many there are
        REP #$20
        STX ZP_SLEN
        LDA.W #(STRNUM & $FFFF)
        STA ZP_SSTR
        SEP #$20
        LDA #BANK_STR
        STA ZP_SSTR+2
        SEC
        RTS

; STRNUM_FROM: X = a number. Its digits, a minus sign if it has one, go into
; STRNUM and X comes back as the length. The digits come out of the division
; backwards, so they wait on the processor's stack and are written in order.
STRNUM_FROM:
        PHP
        SEP #$20
        REP #$10
        REP #$20
        TXA                     ; the number, which arrived in X
        STA ZP_VAL
        SEP #$20
        LDY.W #0
        LDA ZP_VAL+1            ; the sign, from the number's high byte
        BPL STRNUM_FROM_GO
        LDA #'-'
        STA.L STRNUM            ; the first byte, and it is the only one: no
        INY                     ; `STA.L ...,Y` exists, and `abs,Y` loses the bank
        REP #$20
        LDA ZP_VAL
        EOR.W #$FFFF
        INC A
        STA ZP_VAL
        SEP #$20
STRNUM_FROM_GO:
        STY ZP_CH               ; where the digits begin
        LDX.W #0                ; how many there are
STRNUM_FROM_LOOP:
        REP #$20
        LDA ZP_VAL              ; by ten: ZP_A / 10 -> ZP_A, remainder ZP_CNT
        STA ZP_A
        LDA.W #0
        STA ZP_CNT
        LDY #16
STRNUM_FROM_DIV:
        ASL ZP_A
        ROL ZP_CNT
        LDA ZP_CNT
        CMP.W #10
        BCC STRNUM_FROM_NEXT
        SBC.W #10
        STA ZP_CNT
        INC ZP_A
STRNUM_FROM_NEXT:
        DEY
        BNE STRNUM_FROM_DIV
        LDA ZP_A
        STA ZP_VAL
        LDA ZP_CNT
        SEP #$20
        ORA #'0'
        PHA                     ; the least significant digit first
        INX
        REP #$20
        LDA ZP_VAL
        BNE STRNUM_FROM_LOOP
        SEP #$20
        REP #$20
        LDA.W #(STRNUM & $FFFF)
        STA ZP_SDST
        SEP #$20
        LDA #BANK_STR
        STA ZP_SDST+2
        LDY ZP_CH               ; and written the other way round
STRNUM_FROM_WRITE:
        PLA
        STA [ZP_SDST],Y
        INY
        DEX
        BNE STRNUM_FROM_WRITE
        TYA
        TAX                     ; the length, sign and all
        PLP
        RTS

; STRNUM_VAL: VAL of the string in ZP_SSTR -- the number its digits begin with,
; a minus sign allowed and the first character that is not a digit ending it.
STRNUM_VAL:
        REP #$10
        REP #$20
        LDA.W #0
        STA ZP_VAL
        STA ZP_SGN
        SEP #$20
        LDY.W #0
        CPY ZP_SLEN
        BCS STRNUM_VAL_DONE
        LDA [ZP_SSTR],Y
        CMP #'-'
        BNE STRNUM_VAL_DIGITS
        REP #$20
        LDA.W #1
        STA ZP_SGN
        SEP #$20
        INY
STRNUM_VAL_DIGITS:
        CPY ZP_SLEN
        BCS STRNUM_VAL_DONE
        LDA [ZP_SSTR],Y
        CMP #'0'
        BCC STRNUM_VAL_DONE
        CMP #'9'+1
        BCS STRNUM_VAL_DONE
        SEC
        SBC #'0'
        REP #$20
        AND.W #$00FF
        STA ZP_A                ; the digit
        LDA ZP_VAL
        ASL A
        STA ZP_B
        ASL A
        ASL A                   ; eight times
        CLC
        ADC ZP_B                ; ten times
        CLC
        ADC ZP_A
        STA ZP_VAL
        SEP #$20
        INY
        BRA STRNUM_VAL_DIGITS
STRNUM_VAL_DONE:
        REP #$20
        LDA ZP_SGN
        BEQ STRNUM_VAL_HAVE
        LDA.W #0
        SEC
        SBC ZP_VAL
        STA ZP_VAL
STRNUM_VAL_HAVE:
        LDA ZP_VAL
        TAX
        SEP #$20
        RTS

; ---------------------------------------------------------------------------
; Joining two strings in the temporary.
; ---------------------------------------------------------------------------

; STR_TO_TEMP: the string in ZP_SSTR becomes the temporary, which is where the
; right-hand sides are added to, and ZP_STOT says how long it is so far.
STR_TO_TEMP:
        REP #$20
        LDA ZP_SLEN
        CMP.W #(STR_MAX+1)
        SEP #$20
        BCS STR_TEMP_LONG
        REP #$20
        LDA.W #(STRTMP & $FFFF)
        STA ZP_SDST             ; there is no long,Y: a pointer and an index
        SEP #$20
        LDA #BANK_STR
        STA ZP_SDST+2
        REP #$10
        LDY.W #0
STR_TO_TEMP_LOOP:
        CPY ZP_SLEN
        BCS STR_TO_TEMP_DONE
        LDA [ZP_SSTR],Y
        STA [ZP_SDST],Y
        INY
        BRA STR_TO_TEMP_LOOP
STR_TO_TEMP_DONE:
        REP #$20
        LDA ZP_SLEN
        STA ZP_STOT
        LDA.W #(STRTMP & $FFFF)
        STA ZP_SSTR
        SEP #$20
        LDA #BANK_STR
        STA ZP_SSTR+2
        SEC
        RTS
STR_TEMP_LONG:
        JMP SAY_TOOLONG

; STR_APPEND: the string in ZP_SSTR goes onto the end of the temporary, and
; ZP_SSTR/ZP_SLEN come back describing the longer one.
STR_APPEND:
        REP #$20
        LDA ZP_STOT
        CLC
        ADC ZP_SLEN
        CMP.W #(STR_MAX+1)
        SEP #$20
        BCS STR_APPEND_LONG
        REP #$20
        LDA.W #(STRTMP & $FFFF)
        CLC
        ADC ZP_STOT             ; where the new characters begin
        STA ZP_SDST
        SEP #$20
        LDA #BANK_STR
        STA ZP_SDST+2
        REP #$10
        LDY.W #0
STR_APPEND_LOOP:
        CPY ZP_SLEN
        BCS STR_APPEND_DONE
        LDA [ZP_SSTR],Y
        STA [ZP_SDST],Y
        INY
        BRA STR_APPEND_LOOP
STR_APPEND_DONE:
        REP #$20
        LDA ZP_STOT
        CLC
        ADC ZP_SLEN
        STA ZP_STOT             ; the temporary is that much longer
        STA ZP_SLEN
        LDA.W #(STRTMP & $FFFF)
        STA ZP_SSTR
        SEP #$20
        LDA #BANK_STR
        STA ZP_SSTR+2
        SEC
        RTS
STR_APPEND_LONG:
        JMP SAY_TOOLONG

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
        ; and on into the copy, which is the same for an array element

; STR_COPY: the string in ZP_SSTR goes into the slot at ZP_SDST, length byte
; first, and more characters than a slot holds is refused rather than cut.
STR_COPY:
        REP #$10
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
; subscript use every other scratch there is.
;
; An entry is eight bytes: where the run starts in bank three, how many elements
; it has, how many rows, and how many columns. A one-dimensional array is a
; column of one, which is what lets one rule index both: the element at `(i,j)`
; is `i + j * rows`, counting from nought, with the first subscript varying
; fastest -- the order every BASIC since the first one has used.
ARR_ENTRY:
        PHP
        REP #$20
        LDA ZP_ARR
        AND.W #$00FF
        ASL A
        ASL A
        ASL A                   ; eight bytes an entry
        TAX
        PLP                     ; the caller's widths, and its flags, come back
        RTS

; ARR_ELEM: X = the first subscript, ZP_B = the second (nought for a one-
; dimensional array), ZP_ARR = the letter. ZP_SDST = that element in bank three.
; Carry set, and nothing written, when the letter has no array or a subscript is
; outside it.
ARR_ELEM:
        REP #$20
        TXA
        BMI ARR_ELEM_BAD        ; below nought
        STA ZP_TMP16            ; the first subscript
        JSR ARR_ENTRY
        LDA.L ARRAYS+4,X        ; how many rows
        BEQ ARR_ELEM_BAD        ; nought: DIM never made one
        CMP ZP_TMP16
        BEQ ARR_ELEM_BAD        ; the end is one past the last
        BCC ARR_ELEM_BAD
        LDA.L ARRAYS+6,X        ; and how many columns
        AND.W #$7FFF            ; the top bit says the elements are strings
        CMP ZP_B
        BEQ ARR_ELEM_BAD
        BCC ARR_ELEM_BAD
        LDA ZP_B                ; the element is i + j * rows
        BEQ ARR_ELEM_FIRST_ROW
        STA ZP_A
        LDA.L ARRAYS+4,X
        STA ZP_B
        JSR UMUL16              ; ZP_A = j * rows
        REP #$20                ; the product is a word, so read it as one
        LDA ZP_A
        CLC
        ADC ZP_TMP16
        STA ZP_A
        BRA ARR_ELEM_ADDRESS
ARR_ELEM_FIRST_ROW:
        LDA ZP_TMP16
        STA ZP_A
ARR_ELEM_ADDRESS:
        JSR ARR_ENTRY           ; X again: the multiply used Y
        LDA.L ARRAYS,X          ; where the run starts
        STA ZP_B
        LDA ZP_A
        JSR ARR_SHIFT           ; two bytes an element, or a string slot
        CLC
        ADC ZP_B
        STA ZP_SDST
        SEP #$20
        LDA #BANK_STR
        STA ZP_SDST+2
        CLC
        RTS
ARR_ELEM_BAD:
        SEP #$20
        SEC
        RTS

; PARSE_ELEM: ZP_ARR = a letter, and the text at `(i)` or `(i,j)`. The pointer
; steps past the closing bracket and ZP_SDST = the element. Carry set, and
; nothing written, when the letter has no array or a subscript is outside it.
PARSE_ELEM:
        JSR SRC_STEP            ; past the opening bracket
        JSR EXPR                ; X = the first subscript
        PHX
        REP #$20
        LDA.W #0
        STA ZP_B                ; no second subscript yet
        SEP #$20
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #','
        BNE PARSE_ELEM_CLOSE
        JSR SRC_STEP
        JSR EXPR                ; X = the second
        REP #$20
        TXA
        STA ZP_B
        SEP #$20
PARSE_ELEM_CLOSE:
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE PARSE_ELEM_BAD
        JSR SRC_STEP
        PLX
        JMP ARR_ELEM            ; the element, or carry set
PARSE_ELEM_BAD:
        PLX                     ; the first subscript, still waiting
        SEP #$20
        SEC
        RTS

; ARR_SHIFT: A = an element's number, ZP_ARR = the letter. A = its offset in
; the run: two bytes for a word, and a whole string slot for a string.
ARR_SHIFT:
        PHP
        REP #$20
        STA ZP_TMP16
        JSR ARR_ENTRY
        LDA.L ARRAYS+6,X
        AND.W #$8000
        BEQ ARR_SHIFT_WORD
        LDA ZP_TMP16
        ASL A
        ASL A
        ASL A
        ASL A
        ASL A                   ; STR_SLOT bytes an element
        PLP
        RTS
ARR_SHIFT_WORD:
        LDA ZP_TMP16
        ASL A
        PLP
        RTS

; ARR_IS_STRING: ZP_ARR = a letter. Carry set when that array holds strings.
ARR_IS_STRING:
        PHP
        REP #$20
        JSR ARR_ENTRY
        LDA.L ARRAYS+6,X
        AND.W #$8000
        BEQ ARR_IS_STRING_NO
        PLP
        SEC
        RTS
ARR_IS_STRING_NO:
        PLP
        CLC
        RTS

; DO_DIM: `DIM A(10)` gives A eleven elements, nought to ten, which is what the
; 1982 machine's automatic eleven meant; `DIM A(2,3)` gives twelve, indexed
; `A(i,j)` with the first subscript varying fastest and each counting from
; nought. One letter, one line, and the DIM comes before the use.
DO_DIM:
        JSR SKIPSP
        JSR IS_ALPHA
        BCC DIM_BAD
        LDA [ZP_SRC]
        JSR TO_UPPER
        SEC
        SBC #'A'
        STA ZP_VAR              ; kept there: the bounds are expressions
        JSR SRC_STEP
        LDA [ZP_SRC]
        CMP #'$'                ; `DIM A$(3)` is an array of strings
        BNE DIM_OPEN
        LDA ZP_VAR
        ORA #$80                ; the top bit of the letter says so
        STA ZP_VAR
        JSR SRC_STEP
DIM_OPEN:
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'('
        BNE DIM_BAD
        JSR SRC_STEP
        JSR EXPR                ; X = the first bound
        PHX                     ; it waits on the stack: the second bound is an
        JSR SKIPSP              ; expression too, and expressions use every byte
        LDA [ZP_SRC]            ; of the scratch this could have been kept in
        CMP #','
        BEQ DIM_SECOND
        REP #$20
        LDA.W #1
        STA ZP_B                ; one column, until a second bound says otherwise
        PLX                     ; the rows come back
        BRA DIM_CHECK
DIM_SECOND:
        JSR SRC_STEP
        JSR EXPR                ; X = the second bound
        REP #$20
        TXA
        STA ZP_B                ; the columns, waiting their turn to be checked
        PLX                     ; and the rows
DIM_CHECK:
        JSR SKIPSP
        SEP #$20                ; the bounds have been read as words
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
        STA ZP_TMP16            ; the rows, nought included
        LDA ZP_B
        BMI DIM_BAD_WIDE
        CMP.W #ARR_MAX
        BCS DIM_BAD_WIDE
        INC A
        STA ZP_CNT2             ; and the columns
        SEP #$20
        LDA ZP_VAR
        AND #$80                ; the top bit of the letter says the elements are
        BEQ DIM_COLUMNS         ; strings, and the mark rides on the columns
        LDA ZP_CNT2+1
        ORA #$80
        STA ZP_CNT2+1
DIM_COLUMNS:
        REP #$20
        ; from here nothing parses an expression, so the shape stays put
        LDA ZP_TMP16
        STA ZP_A
        LDA ZP_CNT2
        AND.W #$7FFF
        STA ZP_B
        ; the elements are rows times columns, added up rather than multiplied:
        ; the sum stops the moment it passes what the heap could ever hold, and
        ; a product that would overflow sixteen bits stops it even sooner.
        LDA.W #0
        STA ZP_CNT              ; the elements so far
DIM_TIMES:
        LDA ZP_B                ; columns still to add
        BEQ DIM_TIMES_DONE
        LDA ZP_CNT
        CLC
        ADC ZP_A                ; one more column's worth of rows
        CMP.W #ARR_MAX
        BCS DIM_BAD_WIDE
        STA ZP_CNT
        DEC ZP_B
        BRA DIM_TIMES
DIM_TIMES_DONE:
        LDA ZP_CNT
        BNE DIM_HAVE_SIZE       ; an array of nothing is not an array
        BRA DIM_BAD_WIDE
DIM_HAVE_SIZE:
        SEP #$20
        LDA ZP_VAR
        AND #$7F
        STA ZP_ARR              ; the bounds may have been array elements
        REP #$20
        JSR ARR_ENTRY
        ; a run of the same size or less is already the letter's, and stays so
        LDA.L ARRAYS,X
        BEQ DIM_TAKE            ; never dimensioned: it needs a run
        LDA.L ARRAYS+2,X        ; how many elements it has now
        CMP ZP_CNT
        BCC DIM_TAKE            ; smaller than the new one: a new run
        LDA ZP_CNT
        STA.L ARRAYS+2,X        ; as many as were asked for, same place
        BRA DIM_SHAPE
DIM_TAKE:
        LDA.L ARR_TOP           ; where the heap is
        STA ZP_B                ; the run will begin there
        LDA ZP_CNT
        JSR DIM_BYTES           ; two bytes an element, or a whole string slot
        BEQ DIM_NOROOM          ; more elements than a slot each could ever hold
        CLC
        ADC ZP_B
        CMP.W #ARR_LIMIT
        BCS DIM_NOROOM
        STA.L ARR_TOP           ; the heap has grown
        STA ZP_A                ; and the run ends here
        REP #$10
        LDX ZP_B                ; and begins here
        SEP #$20
        LDA #0
DIM_CLEAR:
        CPX ZP_A
        BCS DIM_CLEARED
        STA.L $030000,X         ; the heap is bank three: only its low half varies
        INX
        BRA DIM_CLEAR
DIM_CLEARED:
        REP #$20                ; the entry is written a word at a time
        JSR ARR_ENTRY
        LDA ZP_B
        STA.L ARRAYS,X          ; where the run starts
        LDA ZP_CNT
        STA.L ARRAYS+2,X        ; and how many elements it holds
DIM_SHAPE:
        JSR ARR_ENTRY
        LDA ZP_TMP16
        STA.L ARRAYS+4,X        ; the rows
        LDA ZP_CNT2
        STA.L ARRAYS+6,X        ; and the columns
        SEP #$20
        RTS
; DIM_BYTES: A = how many elements, sixteen bits wide, and ZP_CNT2 = the
; columns with the string flag on their top bit. A = the bytes the run needs: two
; an element for words, a whole slot for strings -- or nought when that many
; slots would not fit in a word at all, which no heap can hold either.
DIM_BYTES:
        STA ZP_A                ; ZP_TMP16 still holds the rows, for the shape
        LDA ZP_CNT2
        AND.W #$8000
        BEQ DIM_BYTES_WORD
        LDA ZP_A
        CMP.W #(65535/STR_SLOT+1)
        BCS DIM_BYTES_NONE
        ASL A
        ASL A
        ASL A
        ASL A
        ASL A                   ; STR_SLOT bytes an element
        RTS
DIM_BYTES_WORD:
        LDA ZP_A
        ASL A
        RTS
DIM_BYTES_NONE:
        LDA.W #0
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
        LDA ZP_BAD
        BNE EXPR_DONE
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'+'
        BEQ EXPR_ADD
        CMP #'-'
        BEQ EXPR_SUB
EXPR_DONE:
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
        LDA ZP_BAD              ; a factor that was not one has already been
        BNE TERM_DONE           ; named, and reading on would name it again
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'*'
        BEQ TERM_MUL
        CMP #'/'
        BEQ TERM_DIV
TERM_DONE:
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
        ; PEEK, VAL, ASC and LEN all answer with a number, so all four live here.
        LDX.W #(KW_FN & $FFFF)
        LDY.W #KW_FN_END - KW_FN
        JSR MATCHKW
        BCS FACTOR_FN
        LDX.W #(KW_PEEK & $FFFF)
        LDY.W #KW_PEEK_END - KW_PEEK
        JSR MATCHKW
        BCS FACTOR_PEEK
        LDX.W #(KW_VAL & $FFFF)
        LDY.W #KW_VAL_END - KW_VAL
        JSR MATCHKW
        BCS FACTOR_VAL
        LDX.W #(KW_ASC & $FFFF)
        LDY.W #KW_ASC_END - KW_ASC
        JSR MATCHKW
        BCS FACTOR_ASC
        LDX.W #(KW_LEN & $FFFF)
        LDY.W #KW_LEN_END - KW_LEN
        JSR MATCHKW
        BCS FACTOR_LEN
        LDX.W #(KW_ABS & $FFFF)
        LDY.W #KW_ABS_END - KW_ABS
        JSR MATCHKW
        BCS FACTOR_ABS
        LDX.W #(KW_SGN & $FFFF)
        LDY.W #KW_SGN_END - KW_SGN
        JSR MATCHKW
        BCS FACTOR_SGN
        BRA FACTOR_VAR
FACTOR_ISSTR:
        JMP FACTOR_MISMATCH

; ABS(n) and SGN(n). Both take a whole expression in brackets, which is what
; FACTOR_IN_BRACKETS is for: the two smaller functions, LEN and PEEK, have their
; own because one of them reads a string and the other a byte.
FACTOR_ABS:
        JSR FACTOR_IN_BRACKETS
        BCC FACTOR_MISMATCH
        REP #$20
        TXA
        BPL FACTOR_ABS_DONE     ; already nought or more
        EOR.W #$FFFF
        INC A
        TAX
FACTOR_ABS_DONE:
        SEP #$20
        RTS
FACTOR_SGN:
        JSR FACTOR_IN_BRACKETS
        BCC FACTOR_MISMATCH
        REP #$20
        TXA
        BEQ FACTOR_SGN_ZERO
        BMI FACTOR_SGN_NEG
        LDA.W #1
        TAX
        BRA FACTOR_SGN_DONE
FACTOR_SGN_NEG:
        LDA.W #$FFFF
        TAX
FACTOR_SGN_ZERO:
FACTOR_SGN_DONE:
        SEP #$20
        RTS

; FACTOR_IN_BRACKETS: the text is `(expression)`. X = its value and the pointer
; steps past the bracket. Carry set, and nothing taken, when it is not.
FACTOR_IN_BRACKETS:
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'('
        BNE FACTOR_IN_BRACKETS_NO
        JSR SRC_STEP
        JSR EXPR
        PHX
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE FACTOR_IN_BRACKETS_BAD
        JSR SRC_STEP
        PLX
        SEC
        RTS
FACTOR_IN_BRACKETS_BAD:
        PLX
FACTOR_IN_BRACKETS_NO:
        CLC
        RTS
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
; PEEK(addr): the byte at a bank-zero address. The machine is sixteen banks
; deep, but the tube, the I/O page and the program are all in the first one, and
; that is the one a language this size keeps its promises about.
FACTOR_PEEK:
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'('
        BNE FACTOR_MISMATCH
        JSR SRC_STEP
        JSR EXPR
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE FACTOR_MISMATCH
        JSR SRC_STEP
        REP #$20
        TXA
        TAX
        SEP #$20
        LDA.L $000000,X         ; the bank is in the instruction, not in a
                                ; register, whatever the data bank holds
        REP #$20
        STA ZP_A
        LDA ZP_A
        AND.W #$00FF
        TAX
        SEP #$20
        RTS

; VAL(s): the number the digits of a string begin with, a minus sign allowed and
; anything else ending them. `VAL("12X")` is twelve, which is what the 1982
; machine does with it too.
FACTOR_VAL:
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
        JSR STRNUM_VAL
        RTS

FACTOR_ASC:
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
        BEQ FACTOR_ASC_EMPTY    ; a string with no characters in it has no first
        LDA [ZP_SSTR]
        AND.W #$00FF
        TAX
        SEP #$20
        RTS
FACTOR_ASC_EMPTY:
        LDA.W #0
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

; FNA(v): a defined function. The argument is evaluated, put in the letter the
; definition calls it by, and the definition's own text is read as an
; expression; then the letter's old value and the line pointer go back as they
; were. There is one guard byte and not a stack of them, so while a definition
; is being read no definition may be called at all -- which is what rules out
; the one that would otherwise call itself for ever.
;
; Which function it is lives in bank three, not in zero page: an expression has
; been through every byte of zero page by the time the entry is needed again.
FACTOR_FN:
        JSR SKIPSP
        JSR IS_ALPHA
        BCC FACTOR_MISMATCH
        LDA [ZP_SRC]
        JSR TO_UPPER
        SEC
        SBC #'A'
        STA.L DEFNSEL
        JSR SRC_STEP
        JSR DEF_ENTRYADDR_SEL
        LDA.L DEFNS+2,X
        CMP #BANK_STR
        BNE FACTOR_MISMATCH     ; never defined
        LDA.L DEFNS+3,X         ; the letter the argument is called by
        STA.L DEFARG
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #'('
        BNE FACTOR_MISMATCH
        JSR SRC_STEP
        JSR EXPR                ; X = the argument
        REP #$20
        TXA
        STA ZP_SDST             ; kept where the evaluation cannot reach it
        SEP #$20
        JSR SKIPSP
        LDA [ZP_SRC]
        CMP #')'
        BNE FACTOR_MISMATCH
        JSR SRC_STEP
        LDA.L DEFGUARD
        BEQ FACTOR_FN_GO
        JMP FACTOR_MISMATCH     ; a definition while another is being read
FACTOR_FN_GO:
        LDA #1
        STA.L DEFGUARD
        ; the line, three bytes
        LDA ZP_SRC
        PHA
        LDA ZP_SRC+1
        PHA
        LDA ZP_SRC+2
        PHA
        ; the argument goes into the letter's own word, and that letter's old
        ; value waits on the stack
        JSR DEF_ENTRYADDR_SEL   ; X = the entry
        LDA.L DEFARG
        ASL A
        TAY
        REP #$20
        LDA VARS,Y
        PHA                     ; the old value, two bytes
        LDA ZP_SDST
        STA VARS,Y              ; the argument, where the definition looks for it
        LDA.L DEFNS,X
        STA ZP_SRC
        SEP #$20
        LDA.L DEFNS+2,X
        STA ZP_SRC+2
        JSR EXPR                ; X = what the definition says it is
        REP #$20
        TXA
        STA ZP_SDST             ; the answer, out of the way of the tidying up
        SEP #$20
        LDA.L DEFARG
        ASL A
        TAY
        PLA
        STA VARS,Y              ; the old value back, low byte first
        PLA
        STA VARS+1,Y
        PLA
        STA ZP_SRC+2            ; and the line, bank first
        PLA
        STA ZP_SRC+1
        PLA
        STA ZP_SRC
        LDA #0
        STA.L DEFGUARD
        REP #$20
        LDA ZP_SDST
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
        LDA ZP_CH               ; the letter, which the subscripts cannot take
        STA ZP_ARR
        JSR PARSE_ELEM          ; ZP_SDST = the element
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
