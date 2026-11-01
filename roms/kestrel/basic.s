; SPDX-License-Identifier: MIT
;
; basic.s -- the Kestrel's BASIC ROM.
;
; The machine's native tongue and the ROM it boots into. The KERNAL gives this
; code a screen, a keyboard and a disk; everything below -- the prompt, the
; arithmetic, the expression grammar, the variables -- is in this 16 KiB and
; nowhere else.
;
; What this ROM does today: it boots, keeps a program in memory as linked text,
; stores and deletes lines, lists them, runs them, and evaluates expressions --
; literals, variables, brackets, unary minus, + - * /, comparisons, AND/OR/NOT --
; in the machine's own floating point, printing the answer in decimal. The
; statement set beyond LET and PRINT is the next ROM's work; `docs/kestrel/roms.md`
; records exactly where the boundary is.

        .include "kestrel.inc"
        .include "bz.inc"

        .org SOCKET

COLD_START_ENTRY:
        JMP BASIC_COLD
WARM_START_ENTRY:
        JMP BASIC_WARM

; ---------------------------------------------------------------------------
; Booting
; ---------------------------------------------------------------------------

BASIC_COLD:
        LDA #<TXT_BANNER
        STA ZP_STRPTR
        LDA #>TXT_BANNER
        STA ZP_STRPTR+1
        JSR K_PRINT
        JSR NEWPROG
        JMP BASIC_WARM

BASIC_WARM:
        LDA #0
        STA ZP_CURSOR
        LDA #<TXT_READY
        STA ZP_STRPTR
        LDA #>TXT_READY
        STA ZP_STRPTR+1
        JSR K_PRINT
BASIC_LOOP:
        JSR K_GETLINE
        LDA #<INBUF
        STA B_IPTR
        LDA #>INBUF
        STA B_IPTR+1
        JSR SKIPSP
        JSR CURCHAR
        BEQ BASIC_WARM
        CMP #'0'
        BCC BASIC_DIRECT
        CMP #'9'+1
        BCS BASIC_DIRECT
        JMP STORE_LINE
BASIC_DIRECT:
        LDA #0
        STA B_MODE
        JSR RESET_STACKS
BASIC_DIRECT_STMT:
        JSR EXEC_STMT
        JSR SKIPSP
        JSR CURCHAR
        CMP #':'
        BNE BASIC_WARM
        JSR ADV
        JMP BASIC_DIRECT_STMT

; RESET_STACKS: an empty value stack, no loops, no subroutines pending.
RESET_STACKS:
        LDA #0
        STA B_VALSP
        STA B_FORSP
        STA B_GOSUBSP
        STA B_FLAGS
        RTS

; ---------------------------------------------------------------------------
; The program in memory
; ---------------------------------------------------------------------------
;
; A program is a chain of lines. Each opens with the address of the next, then
; its number, then the text as typed, then a zero:
;
;       .word next, .word number, .byte text..., 0
;
; Keeping the text (rather than tokens) is what Atari BASIC did: LIST costs
; nothing, and every statement is re-read as it runs.

NEWPROG:
        LDA #0
        STA B_CANCONT
        LDA #<MEMTOP
        STA B_MEMTOP
        LDA #>MEMTOP
        STA B_MEMTOP+1
        LDA #<PROGSTART
        STA B_TXTTAB
        LDA #>PROGSTART
        STA B_TXTTAB+1
        LDA #<(PROGSTART+2)
        STA B_VARTAB
        LDA #>(PROGSTART+2)
        STA B_VARTAB+1
        JSR CLEARVARS
        LDA #0
        LDY #0
        STA (B_TXTTAB),Y
        INY
        STA (B_TXTTAB),Y
        LDA #0
        STA B_MODE
        RTS

CLEARVARS:
        LDA B_VARTAB
        STA B_ARYTAB
        LDA B_VARTAB+1
        STA B_ARYTAB+1
        LDA #0
        STA B_FRETOP
        STA B_FRETOP+1
        LDA #<MEMTOP
        STA B_STREND
        LDA #>MEMTOP
        STA B_FRETOP+1
        STA B_STREND+1
        LDA #<PROGSTART
        STA B_DATAPTR
        LDA #>PROGSTART
        STA B_DATAPTR+1
        RTS

; FINDLINE: A/Y = a line number -> B_CURLINE, carry clear; carry set if absent.
FINDLINE:
        STA B_INT
        STY B_INT+1
        LDA B_TXTTAB
        STA B_LINEBUF
        LDA B_TXTTAB+1
        STA B_LINEBUF+1
FINDLINE_LOOP:
        ; The lines stop at the end marker, which VARTAB points at. A zero
        ; link only means the line is the last one, so it is still a line.
        LDA B_LINEBUF
        CMP B_VARTAB
        LDA B_LINEBUF+1
        SBC B_VARTAB+1
        BCS FINDLINE_MISS
        LDY #2
        LDA (B_LINEBUF),Y
        CMP B_INT
        BNE FINDLINE_NEXT
        INY
        LDA (B_LINEBUF),Y
        CMP B_INT+1
        BNE FINDLINE_NEXT
        LDA B_LINEBUF
        STA B_CURLINE
        LDA B_LINEBUF+1
        STA B_CURLINE+1
        CLC
        RTS
FINDLINE_NEXT:
        LDY #0
        LDA (B_LINEBUF),Y
        PHA
        INY
        LDA (B_LINEBUF),Y
        STA B_LINEBUF+1
        PLA
        STA B_LINEBUF
        ORA B_LINEBUF+1          ; a zero link is the end of the program, and
        BEQ FINDLINE_MISS        ; walking past it reads the zero page as lines
        JMP FINDLINE_LOOP
FINDLINE_MISS:
        SEC
        RTS

; PARSELINENO: a decimal number at (B_IPTR) into B_INT.
PARSELINENO:
        LDA #0
        STA B_INT
        STA B_INT+1
        STA B_TEMP3
PARSELINENO_LOOP:
        JSR CURCHAR
        CMP #'0'
        BCC PARSELINENO_END
        CMP #'9'+1
        BCS PARSELINENO_END
        SEC
        SBC #'0'
        STA B_CHAR
        JSR MUL10INT
        CLC
        LDA B_INT
        ADC B_CHAR
        STA B_INT
        LDA B_INT+1
        ADC #0
        STA B_INT+1
        INC B_TEMP3
        JSR ADV
        JMP PARSELINENO_LOOP
PARSELINENO_END:
        LDA B_TEMP3
        BEQ PARSELINENO_FAIL
        CLC
        RTS
PARSELINENO_FAIL:
        SEC
        RTS

MUL10INT:
        LDA B_INT
        STA B_TEMP
        LDA B_INT+1
        STA B_TEMP2
        ASL B_TEMP
        ROL B_TEMP2             ; twice
        LDA B_INT
        ASL A
        ROL B_INT+1
        ASL A
        ROL B_INT+1
        ASL A
        ROL B_INT+1
        STA B_INT               ; eight times
        CLC
        LDA B_INT
        ADC B_TEMP
        STA B_INT
        LDA B_INT+1
        ADC B_TEMP2
        STA B_INT+1
        RTS

; ---------------------------------------------------------------------------
; Storing a line
; ---------------------------------------------------------------------------
;
; The program is rebuilt into scratch memory at $4000 on every change: the old
; lines are copied across in order, the new one dropped in where its number
; belongs, and the result copied back. It costs a second copy of the program and
; buys a routine with no shifting, no sign handling and nothing to get wrong.

SCRATCH     = $4000

; STORE_LINE: the numbered line at (B_IPTR) enters the program in order,
; replacing a line of the same number. A number with nothing after it deletes.
STORE_LINE:
        JSR PARSELINENO
        BCS ERR_SYNTAX
        JSR SKIPSP
        LDA B_IPTR
        STA B_TMPP
        LDA B_IPTR+1
        STA B_TMPP+1
        LDY #0
STORE_TEXTLEN:
        LDA (B_TMPP),Y
        BEQ STORE_TEXTLEN_DONE
        INY
        BNE STORE_TEXTLEN
STORE_TEXTLEN_DONE:
        STY B_TEMP
        LDA #0
        STA B_FLAGS
        LDA #<SCRATCH
        STA B_TMPP2
        LDA #>SCRATCH
        STA B_TMPP2+1

        ; An empty program is exactly its two-byte end marker. The link being
        ; zero is not the test: a one-line program's link is zero too.
        CLC
        LDA B_TXTTAB
        ADC #2
        STA B_TMP
        LDA B_TXTTAB+1
        ADC #0
        STA B_TMP+1
        LDA B_TMP
        CMP B_VARTAB
        LDA B_TMP+1
        SBC B_VARTAB+1
        BCS STORE_FINISH
        LDA B_TXTTAB
        STA B_LINEBUF
        LDA B_TXTTAB+1
        STA B_LINEBUF+1

STORE_WALK:
        LDY #2
        LDA (B_LINEBUF),Y
        CMP B_INT
        INY
        LDA (B_LINEBUF),Y
        SBC B_INT+1
        BCC STORE_COPY_OLD
        LDA B_FLAGS
        AND #1
        BNE STORE_SKIP_EQUAL
        JSR STORE_EMIT_NEW
STORE_SKIP_EQUAL:
        LDY #2
        LDA (B_LINEBUF),Y
        CMP B_INT
        BNE STORE_COPY_OLD
        INY
        LDA (B_LINEBUF),Y
        CMP B_INT+1
        BNE STORE_COPY_OLD
        JSR STORE_ADVANCE
        LDA B_LINEBUF
        ORA B_LINEBUF+1
        BEQ STORE_FINISH
        JMP STORE_WALK
STORE_COPY_OLD:
        JSR STORE_COPY_LINE
        JSR STORE_ADVANCE
        LDA B_LINEBUF
        ORA B_LINEBUF+1
        BEQ STORE_FINISH
        JMP STORE_WALK

STORE_FINISH:
        LDA B_FLAGS
        AND #1
        BNE STORE_INSTALL
        JSR STORE_EMIT_NEW
STORE_INSTALL:
        LDA #<SCRATCH
        STA B_LINEBUF
        LDA #>SCRATCH
        STA B_LINEBUF+1
        LDA B_TXTTAB
        STA B_TMPP
        LDA B_TXTTAB+1
        STA B_TMPP+1
STORE_INSTALL_LOOP:
        LDA B_LINEBUF
        CMP B_TMPP2
        LDA B_LINEBUF+1
        SBC B_TMPP2+1
        BCS STORE_INSTALL_DONE
        LDY #0
        LDA (B_LINEBUF),Y
        STA (B_TMPP),Y
        INC B_LINEBUF
        BNE STORE_INSTALL_SRC
        INC B_LINEBUF+1
STORE_INSTALL_SRC:
        INC B_TMPP
        BNE STORE_INSTALL_LOOP
        INC B_TMPP+1
        JMP STORE_INSTALL_LOOP
STORE_INSTALL_DONE:
        LDA B_TMPP
        STA B_VARTAB
        LDA B_TMPP+1
        STA B_VARTAB+1
        JSR LINKPROG
        JMP BASIC_WARM

; STORE_EMIT_NEW: the new line goes into the scratch, unless it is a deletion.
STORE_EMIT_NEW:
        LDA #1
        STA B_FLAGS
        LDA B_TEMP
        BNE STORE_EMIT
        RTS
STORE_EMIT:
        ; A line is [link][number][text][0]; LINKPROG fills the link in later.
        LDY #0
        LDA #0
        STA (B_TMPP2),Y
        INY
        STA (B_TMPP2),Y
        INY
        LDA B_INT
        STA (B_TMPP2),Y
        INY
        LDA B_INT+1
        STA (B_TMPP2),Y
        CLC
        LDA B_TMPP2
        ADC #4
        STA B_TMPP2
        LDA B_TMPP2+1
        ADC #0
        STA B_TMPP2+1
        LDY #0
STORE_EMIT_TEXT:
        CPY B_TEMP
        BEQ STORE_EMIT_END
        LDA (B_IPTR),Y
        STA (B_TMPP2),Y
        INY
        JMP STORE_EMIT_TEXT
STORE_EMIT_END:
        LDA #0
        STA (B_TMPP2),Y
        TYA
        CLC
        ADC B_TMPP2
        STA B_TMPP2
        LDA B_TMPP2+1
        ADC #0
        STA B_TMPP2+1
        INC B_TMPP2
        BNE STORE_EMIT_RTS
        INC B_TMPP2+1
STORE_EMIT_RTS:
        RTS

; STORE_COPY_LINE: the whole line at B_LINEBUF goes into the scratch at
; B_TMPP2. The walk cursor is left alone so the caller can still follow the
; line's link; only the scratch cursor moves on.
STORE_COPY_LINE:
        LDA B_LINEBUF
        STA B_TMPP
        LDA B_LINEBUF+1
        STA B_TMPP+1
        ; the text begins after the four-byte header; scan that for its end
        LDA B_LINEBUF
        CLC
        ADC #4
        STA B_TMP2
        LDA B_LINEBUF+1
        ADC #0
        STA B_TMP2+1
        LDY #0
STORE_COPY_LINE_SCAN:
        LDA (B_TMP2),Y
        BEQ STORE_COPY_LINE_LEN
        INY
        BNE STORE_COPY_LINE_SCAN
STORE_COPY_LINE_LEN:
        ; Y is the terminator's offset from the text, so the line is Y+5 bytes
        TYA
        CLC
        ADC #5
        STA B_TEMP2
        LDA #0
        STA B_TEMP2+1
STORE_COPY_LINE_LOOP:
        LDY #0
        LDA (B_TMPP),Y
        STA (B_TMPP2),Y
        INC B_TMPP
        BNE STORE_COPY_LINE_DST
        INC B_TMPP+1
STORE_COPY_LINE_DST:
        INC B_TMPP2
        BNE STORE_COPY_LINE_TICK
        INC B_TMPP2+1
STORE_COPY_LINE_TICK:
        LDA B_TEMP2
        BNE STORE_COPY_LINE_DEC
        DEC B_TEMP2+1
STORE_COPY_LINE_DEC:
        DEC B_TEMP2
        LDA B_TEMP2
        ORA B_TEMP2+1
        BNE STORE_COPY_LINE_LOOP
        RTS

; STORE_ADVANCE: B_LINEBUF follows its line's link.
STORE_ADVANCE:
        LDY #0
        LDA (B_LINEBUF),Y
        PHA
        INY
        LDA (B_LINEBUF),Y
        STA B_LINEBUF+1
        PLA
        STA B_LINEBUF
        RTS

; LINKPROG: every line's first two bytes point at the next line, or at zero.
LINKPROG:
        LDA B_TXTTAB
        STA B_LINEBUF
        LDA B_TXTTAB+1
        STA B_LINEBUF+1
LINKPROG_LOOP:
        ; stop when this position is at or past the end of the program
        CLC
        LDA B_LINEBUF
        ADC #2
        STA B_TMP
        LDA B_LINEBUF+1
        ADC #0
        STA B_TMP+1
        LDA B_TMP
        CMP B_VARTAB
        LDA B_TMP+1
        SBC B_VARTAB+1
        BCS LINKPROG_DONE
        ; find the terminator of this line's text
        CLC
        LDA B_LINEBUF
        ADC #4
        STA B_TMPP
        LDA B_LINEBUF+1
        ADC #0
        STA B_TMPP+1
        LDY #0
LINKPROG_TEXT:
        LDA (B_TMPP),Y
        BEQ LINKPROG_TEXT_END
        INY
        JMP LINKPROG_TEXT
LINKPROG_TEXT_END:
        ; next = line + 4 (the header) + Y (the text) + 1 (the terminator)
        TYA
        CLC
        ADC #5
        STA B_TMP
        LDA #0
        STA B_TMP+1
        CLC
        LDA B_TMP
        ADC B_LINEBUF
        STA B_TMP
        LDA B_TMP+1
        ADC B_LINEBUF+1
        STA B_TMP+1
        LDA B_TMP
        CMP B_VARTAB
        LDA B_TMP+1
        SBC B_VARTAB+1
        BCC LINKPROG_STORE
        LDA #0
        STA B_TMP
        STA B_TMP+1
LINKPROG_STORE:
        LDY #0
        LDA B_TMP
        STA (B_LINEBUF),Y
        INY
        LDA B_TMP+1
        STA (B_LINEBUF),Y
        LDA B_TMP
        ORA B_TMP+1
        BEQ LINKPROG_DONE
        LDA B_TMP
        STA B_LINEBUF
        LDA B_TMP+1
        STA B_LINEBUF+1
        JMP LINKPROG_LOOP
LINKPROG_DONE:
        RTS

; ---------------------------------------------------------------------------
; Running
; ---------------------------------------------------------------------------

; PROGEMPTY: carry set when no lines are stored. The end of the program is
; found when a line's link runs out, so this is the only place the two-byte
; end marker matters.
PROGEMPTY:
        CLC
        LDA B_TXTTAB
        ADC #2
        STA B_TMP
        LDA B_TXTTAB+1
        ADC #0
        STA B_TMP+1
        LDA B_TMP
        CMP B_VARTAB
        LDA B_TMP+1
        SBC B_VARTAB+1
        RTS

DO_RUN:
        LDA #0
        STA B_CANCONT
        JSR CLEARVARS
        JSR RESET_STACKS
        LDA #1
        STA B_MODE
        LDA B_TXTTAB
        STA B_CURLINE
        LDA B_TXTTAB+1
        STA B_CURLINE+1
        JSR PROGEMPTY
        BCS RUN_END
RUN_LOOP:
        LDY #2
        LDA (B_CURLINE),Y
        STA B_LINENO
        INY
        LDA (B_CURLINE),Y
        STA B_LINENO+1
        LDA B_CURLINE
        CLC
        ADC #4
        STA B_IPTR
        LDA B_CURLINE+1
        ADC #0
        STA B_IPTR+1
RUN_STMT:
        JSR SKIPSP
        JSR CURCHAR
        BEQ RUN_ADVANCE
        CMP #':'
        BNE RUN_EXEC
        JSR ADV
        JMP RUN_STMT
RUN_EXEC:
        JSR EXEC_STMT
        BCS RUN_CONTROL
        JMP RUN_STMT
RUN_CONTROL:
        LDA B_FLAGS
        AND #1
        BNE RUN_JUMPED
        JMP RUN_STMT
RUN_JUMPED:
        LDA #0
        STA B_FLAGS
        JMP RUN_LOOP
RUN_ADVANCE:
        LDY #0
        LDA (B_CURLINE),Y
        PHA
        INY
        LDA (B_CURLINE),Y
        STA B_CURLINE+1
        PLA
        STA B_CURLINE
        LDA B_CURLINE
        ORA B_CURLINE+1
        BNE RUN_LOOP
RUN_END:
        JMP BASIC_WARM

; ---------------------------------------------------------------------------
; Statement dispatch
; ---------------------------------------------------------------------------

; MATCHKW: the keyword at (ZP_PTR2) matches at (B_IPTR)? The word must end
; where the keyword does, so PRINT does not match PRINTA.
MATCHKW:
        LDA B_IPTR
        STA B_TMPP
        LDA B_IPTR+1
        STA B_TMPP+1
        LDY #0
MATCHKW_LOOP:
        LDA (ZP_PTR2),Y
        BEQ MATCHKW_BOUNDARY
        PHA
        LDA (B_TMPP),Y
        JSR UPPER
        STA B_TEMP
        PLA
        CMP B_TEMP
        BNE MATCHKW_NO
        INY
        JMP MATCHKW_LOOP
MATCHKW_BOUNDARY:
        LDA (B_TMPP),Y
        JSR UPPER
        CMP #'A'
        BCC MATCHKW_YES
        CMP #'Z'+1
        BCS MATCHKW_YES
        CLC
        RTS
MATCHKW_YES:
        TYA
        CLC
        ADC B_IPTR
        STA B_IPTR
        LDA B_IPTR+1
        ADC #0
        STA B_IPTR+1
        SEC
        RTS
MATCHKW_NO:
        CLC
        RTS

TRY:
        STA ZP_PTR2
        STY ZP_PTR2+1
        JMP MATCHKW

; EXEC_STMT: one statement at (B_IPTR). Carry set when control moved.
EXEC_STMT:
        JSR SKIPSP
        JSR CURCHAR
        BEQ EXEC_END
        CMP #':'
        BEQ EXEC_END
        CMP #'?'
        BEQ DO_PRINT
        JSR LOOKSLIKE_LET
        BCS DO_LET
        LDA #<KW_PRINT
        LDY #>KW_PRINT
        JSR TRY
        BCS DO_PRINT
        LDA #<KW_IF
        LDY #>KW_IF
        JSR TRY
        BCS DO_IF
        LDA #<KW_GOTO
        LDY #>KW_GOTO
        JSR TRY
        BCS DO_GOTO
        LDA #<KW_GOSUB
        LDY #>KW_GOSUB
        JSR TRY
        BCS DO_GOSUB
        LDA #<KW_RETURN
        LDY #>KW_RETURN
        JSR TRY
        BCS DO_RETURN
        LDA #<KW_FOR
        LDY #>KW_FOR
        JSR TRY
        BCS DO_FOR
        LDA #<KW_NEXT
        LDY #>KW_NEXT
        JSR TRY
        BCS DO_NEXT
        LDA #<KW_INPUT
        LDY #>KW_INPUT
        JSR TRY
        BCS DO_INPUT
        LDA #<KW_DEF
        LDY #>KW_DEF
        JSR TRY
        BCS DO_DEF
        LDA #<KW_DIM
        LDY #>KW_DIM
        JSR TRY
        BCS DO_DIM
        LDA #<KW_GET
        LDY #>KW_GET
        JSR TRY
        BCS DO_GET
        LDA #<KW_REM
        LDY #>KW_REM
        JSR TRY
        BCS DO_REM
        LDA #<KW_END
        LDY #>KW_END
        JSR TRY
        BCS DO_END
        LDA #<KW_STOP
        LDY #>KW_STOP
        JSR TRY
        BCS DO_STOP
        LDA #<KW_CONT
        LDY #>KW_CONT
        JSR TRY
        BCS DO_CONT
        LDA #<KW_DATA
        LDY #>KW_DATA
        JSR TRY
        BCS DO_REM
        LDA #<KW_READ
        LDY #>KW_READ
        JSR TRY
        BCS DO_READ
        LDA #<KW_RESTORE
        LDY #>KW_RESTORE
        JSR TRY
        BCS DO_RESTORE
        LDA #<KW_CLS
        LDY #>KW_CLS
        JSR TRY
        BCS DO_CLS
        LDA #<KW_POKE
        LDY #>KW_POKE
        JSR TRY
        BCS DO_POKE
        LDA #<KW_ON
        LDY #>KW_ON
        JSR TRY
        BCS DO_ON
        LDA #<KW_DELETE
        LDY #>KW_DELETE
        JSR TRY
        BCS DO_DELETE
        LDA #<KW_LOAD
        LDY #>KW_LOAD
        JSR TRY
        BCS DO_LOAD
        LDA #<KW_SAVE
        LDY #>KW_SAVE
        JSR TRY
        BCS DO_SAVE
        LDA #<KW_CATALOG
        LDY #>KW_CATALOG
        JSR TRY
        BCS DO_CATALOG
        LDA #<KW_LIST
        LDY #>KW_LIST
        JSR TRY
        BCS DO_LIST
        LDA #<KW_RUN
        LDY #>KW_RUN
        JSR TRY
        BCS DO_RUN
        LDA #<KW_NEW
        LDY #>KW_NEW
        JSR TRY
        BCS DO_NEW
        LDA #<KW_HELP
        LDY #>KW_HELP
        JSR TRY
        BCS DO_HELP
        LDA #<KW_LET
        LDY #>KW_LET
        JSR TRY
        BCS DO_LET
        JMP ERR_UNDEF_STATEMENT
EXEC_END:
        CLC
        RTS

; LOOKSLIKE_LET: a name followed by '='.
LOOKSLIKE_LET:
        LDA B_IPTR
        STA B_TMPP
        LDA B_IPTR+1
        STA B_TMPP+1
        LDY #0
        LDA (B_TMPP),Y
        JSR UPPER
        CMP #'A'
        BCC LOOKSLIKE_NO
        CMP #'Z'+1
        BCS LOOKSLIKE_NO
        INY
        LDA (B_TMPP),Y
        CMP #'$'
        BEQ LOOKSLIKE_SKIP
        JSR UPPER
        CMP #'A'
        BCC LOOKSLIKE_MAYBEDIGIT
        CMP #'Z'+1
        BCC LOOKSLIKE_SKIP
LOOKSLIKE_MAYBEDIGIT:
        CMP #'0'
        BCC LOOKSLIKE_TWO
        CMP #'9'+1
        BCS LOOKSLIKE_TWO
LOOKSLIKE_SKIP:
        INY
LOOKSLIKE_TWO:
        LDA (B_TMPP),Y
        CMP #'='
        BEQ LOOKSLIKE_YES
        CMP #'('
        BNE LOOKSLIKE_NO       ; A(3)=1 is an assignment too
LOOKSLIKE_YES:
        SEC
        RTS
LOOKSLIKE_NO:
        CLC
        RTS

; ---------------------------------------------------------------------------
; The statements
; ---------------------------------------------------------------------------

DO_PRINT:
        JSR SKIPSP
        JSR CURCHAR
        CMP #'?'
        BNE PRINT_ITEMS
        JSR ADV
PRINT_ITEMS:
        JSR SKIPSP
        JSR CURCHAR
        BEQ PRINT_NL
        CMP #':'
        BEQ PRINT_NL
        CMP #'"'
        BEQ PRINT_STRING
        JSR EXPR
        LDA B_FLAGS
        AND #2
        BNE PRINT_ITEMS_STRING
        JSR FPRINT
        LDA #<B_NUMBUF
        STA ZP_STRPTR
        LDA #>B_NUMBUF
        STA ZP_STRPTR+1
        JSR K_PRINT
        JMP PRINT_SEPARATOR
PRINT_ITEMS_STRING:
        JSR STRPRINT
        JMP PRINT_SEPARATOR
PRINT_STRING:
        JSR ADV                 ; past the opening quote
PRINT_STRING_LOOP:
        JSR CURCHAR
        BEQ PRINT_NL            ; an unterminated string ends the statement
        CMP #'"'
        BEQ PRINT_STRING_END
        JSR K_CHROUT
        JSR ADV
        JMP PRINT_STRING_LOOP
PRINT_STRING_END:
        JSR ADV                 ; past the closing quote
PRINT_SEPARATOR:
        JSR SKIPSP
        JSR CURCHAR
        CMP #','
        BEQ PRINT_COMMA
        CMP #';'
        BEQ PRINT_SEMI
        JMP PRINT_NL
PRINT_COMMA:
        JSR ADV
        JSR TABZONE
        JMP PRINT_NEXT
PRINT_SEMI:
        JSR ADV
PRINT_NEXT:
        JSR SKIPSP
        JSR CURCHAR
        BEQ PRINT_NL
        CMP #':'
        BEQ PRINT_NL
        JMP PRINT_ITEMS
PRINT_NL:
        LDA #CC_RETURN
        JMP K_CHROUT

TABZONE:
        LDA ZP_COL
        AND #7
        BEQ TABZONE_DONE
        LDA #' '
        JSR K_CHROUT
        JMP TABZONE
TABZONE_DONE:
        RTS

DO_LET:
        JSR PARSENAME
        BCS ERR_SYNTAX
        LDA B_TEMP
        STA B_VNAME1
        LDA B_TEMP2
        STA B_VNAME2
        LDA B_TEMP3
        STA B_VNAMET
        JSR SKIPSP
        JSR CURCHAR
        CMP #'('
        BEQ LET_ARRAY
        CMP #'='
        BNE ERR_SYNTAX
        JSR ADV
        LDA B_VNAMET
        CMP #T_STR
        BEQ LET_STRING
        JSR EXPR
        JSR STRNONE
        LDA B_VNAME1
        STA B_TEMP
        LDA B_VNAME2
        STA B_TEMP2
        LDA B_VNAMET
        STA B_TEMP3
        JMP VARSET
LET_ARRAY:
        LDA B_VNAME1
        STA B_TEMP
        LDA B_VNAME2
        STA B_TEMP2
        LDA B_VNAMET
        STA B_TEMP3
        JSR ARY_PUT
        SEC
        RTS
LET_STRING:
        JSR EXPR
        LDA B_VNAME1
        STA B_TEMP
        LDA B_VNAME2
        STA B_TEMP2
        LDA B_VNAMET
        STA B_TEMP3
        JMP STRASSIGN

; --- GOTO, GOSUB, RETURN ---------------------------------------------------

DO_GOTO:
        JSR EXPR
        JSR FTOI
        BCS ERR_SYNTAX
        JSR FINDLINE
        BCS ERR_UNDEF_LINE
        LDA #1
        STA B_FLAGS
        SEC
        RTS

DO_GOSUB:
        JSR EXPR
        JSR FTOI
        BCS ERR_SYNTAX
        STA B_INT
        STY B_INT+1
        LDX B_GOSUBSP
        CPX #GOSUB_MAX
        BCS ERR_OUT_OF_MEMORY
        TXA
        ASL A
        ASL A
        TAX
        LDA B_CURLINE
        STA GOSUBSTK,X
        LDA B_CURLINE+1
        STA GOSUBSTK+1,X
        LDA B_IPTR
        STA GOSUBSTK+2,X
        LDA B_IPTR+1
        STA GOSUBSTK+3,X
        INC B_GOSUBSP
        LDA B_INT
        LDY B_INT+1
        JSR FINDLINE
        BCS ERR_UNDEF_LINE
        LDA #1
        STA B_FLAGS
        SEC
        RTS

DO_RETURN:
        LDA B_GOSUBSP
        BEQ ERR_RETURN
        DEC B_GOSUBSP
        LDA B_GOSUBSP
        ASL A
        ASL A
        TAX
        LDA GOSUBSTK,X
        STA B_CURLINE
        LDA GOSUBSTK+1,X
        STA B_CURLINE+1
        LDA GOSUBSTK+2,X
        STA B_IPTR
        LDA GOSUBSTK+3,X
        STA B_IPTR+1
        LDA #0
        STA B_FLAGS
        SEC
        RTS

; --- IF --------------------------------------------------------------------

DO_IF:
        JSR EXPR
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BEQ IF_FALSE
        ; The THEN is optional, as it was on machines of the period.
        LDA #<KW_THEN
        LDY #>KW_THEN
        JSR TRY
        JSR SKIPSP
        JSR CURCHAR
        CMP #'0'
        BCC IF_RUN_REST
        CMP #'9'+1
        BCS IF_RUN_REST
        JSR PARSELINENO
        BCS ERR_SYNTAX
        LDA B_INT               ; FINDLINE takes the number in A and Y
        LDY B_INT+1
        JSR FINDLINE
        BCS ERR_UNDEF_LINE
        LDA #1
        STA B_FLAGS
        SEC
        RTS
IF_RUN_REST:
        ; The rest of the line is the statement to run.
        CLC
        RTS
IF_FALSE:
        ; Skip to ELSE at this level, or to the end of the line.
IF_SKIP_LOOP:
        JSR SKIPSP
        JSR CURCHAR
        BEQ IF_FALSE_END
        CMP #'"'
        BEQ IF_SKIP_STRING
        LDA #<KW_ELSE
        LDY #>KW_ELSE
        JSR TRY
        BCS IF_ELSE
        JSR ADV
        JMP IF_SKIP_LOOP
IF_SKIP_STRING:
        JSR ADV
IF_SKIP_STRING_LOOP:
        JSR CURCHAR
        BEQ IF_FALSE_END
        CMP #'"'
        BEQ IF_SKIP_STRING_END
        JSR ADV
        JMP IF_SKIP_STRING_LOOP
IF_SKIP_STRING_END:
        JSR ADV
        JMP IF_SKIP_LOOP
IF_ELSE:
        CLC
        RTS
IF_FALSE_END:
        ; Nothing left on this line: point at the end so the driver advances.
        CLC
        RTS

; --- FOR / NEXT ------------------------------------------------------------

; A loop's frame is sixteen bytes:
;       0-1 name, 2-6 limit, 7-11 step, 12-13 line, 14-15 where to resume
DO_FOR:
        JSR PARSENAME
        BCS ERR_SYNTAX
        LDA B_TEMP
        STA B_VNAME1
        LDA B_TEMP2
        STA B_VNAME2
        LDA B_TEMP3
        STA B_VNAMET
        JSR SKIPSP
        JSR CURCHAR
        CMP #'='
        BNE ERR_SYNTAX
        JSR ADV
        JSR EXPR
        JSR FOR_VARSET
        LDA #<KW_TO
        LDY #>KW_TO
        JSR TRY
        BCS FOR_LIMIT
        JMP ERR_SYNTAX
FOR_LIMIT:
        JSR EXPR
        LDX B_FORSP
        CPX #FORSTK_MAX
        BCS ERR_OUT_OF_MEMORY
        JSR FORFRAME
        LDY #2
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
        ; STEP, or one
        LDA #<KW_STEP
        LDY #>KW_STEP
        JSR TRY
        BCS FOR_STEP
        LDA #129
        STA B_FAC
        LDA #0
        STA B_FAC+1
        STA B_FAC+2
        STA B_FAC+3
        STA B_FAC+4
        JMP FOR_STORE_STEP
FOR_STEP:
        JSR EXPR
FOR_STORE_STEP:
        JSR FORFRAME
        LDY #7
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
        LDY #0
        LDA B_VNAME1
        STA (B_TMPP),Y
        INY
        LDA B_VNAME2
        STA (B_TMPP),Y
        LDA B_CURLINE
        LDY #12
        STA (B_TMPP),Y
        INY
        LDA B_CURLINE+1
        STA (B_TMPP),Y
        LDA B_IPTR
        LDY #14
        STA (B_TMPP),Y
        INY
        LDA B_IPTR+1
        STA (B_TMPP),Y
        INC B_FORSP
        JSR FOR_MAYBE_EMPTY     ; a loop whose start is past its limit skips
        CLC
        RTS

; FOR_MAYBE_EMPTY: a FOR whose start is already past its limit runs its body no
; times at all, so the frame is dropped and the parser is moved past the NEXT
; that closes it. STEP 0 and a start equal to the limit both run once, as they
; did on the machines of the period.
FOR_MAYBE_EMPTY:
        LDA B_VNAME1
        STA B_TEMP
        LDA B_VNAME2
        STA B_TEMP2
        LDA #T_NUM
        STA B_TEMP3
        JSR VARREF              ; FAC = the start the variable already holds
        JSR NEXTFRAME
        LDY #2
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
        JSR FCOMP               ; A = -1 below the limit, 0 level, 1 above
        STA B_CHAR
        JSR NEXTFRAME
        LDY #8                  ; bit 7 of the step's mantissa, not its exponent
        LDA (B_TMPP),Y
        BMI FOR_EMPTY_DOWN
        LDA B_CHAR
        CMP #1                  ; counting up: a start above the limit is empty
        BEQ FOR_EMPTY_SKIP
        RTS
FOR_EMPTY_DOWN:
        LDA B_CHAR
        CMP #$FF                ; counting down: a start below it is empty
        BEQ FOR_EMPTY_SKIP
        RTS

FOR_EMPTY_SKIP:
        DEC B_FORSP             ; the frame goes: this loop never ran
        LDA B_VNAME1
        STA B_SCAN1             ; the name to look for
        LDA B_VNAME2
        STA B_SCAN2
        LDA #0
        STA B_SCAND
        LDA B_CURLINE
        STA B_SCANLINE
        LDA B_CURLINE+1
        STA B_SCANLINE+1

; The parser walks forward statement by statement, counting the FORs it meets,
; until it meets the NEXT that belongs to this loop. Strings and remarks are
; stepped over whole, and the walk crosses lines the same way the runner does.
FOR_SKIP_SCAN:
        JSR SKIPSP
        JSR CURCHAR
        BEQ FOR_SKIP_EOL
        CMP #':'
        BNE FOR_SKIP_WORD
        JSR ADV
        JMP FOR_SKIP_SCAN
FOR_SKIP_WORD:
        CMP #'"'
        BEQ FOR_SKIP_STRING
        LDA #<KW_REM
        LDY #>KW_REM
        JSR TRY
        BCS FOR_SKIP_EOL        ; a remark runs to the end of the line
        LDA #<KW_FOR
        LDY #>KW_FOR
        JSR TRY
        BCS FOR_SKIP_DEEPER
        LDA #<KW_NEXT
        LDY #>KW_NEXT
        JSR TRY
        BCC FOR_SKIP_STEP       ; an ordinary word: carry on a character at a time
        LDA B_SCAND
        BNE FOR_SKIP_SHALLOWER
        JSR SKIPSP
        JSR CURCHAR
        BEQ FOR_SKIP_FOUND      ; a bare NEXT closes the innermost loop, this one
        JSR PARSENAME
        BCS FOR_SKIP_STEP
        LDA B_TEMP
        CMP B_SCAN1
        BNE FOR_SKIP_STEP
        LDA B_TEMP2
        CMP B_SCAN2
        BNE FOR_SKIP_STEP
        JMP FOR_SKIP_FOUND
FOR_SKIP_DEEPER:
        INC B_SCAND
        JMP FOR_SKIP_SCAN
FOR_SKIP_SHALLOWER:
        DEC B_SCAND
        JMP FOR_SKIP_SCAN
FOR_SKIP_STEP:
        JSR ADV
        JMP FOR_SKIP_SCAN
FOR_SKIP_STRING:
        JSR ADV
FOR_SKIP_STRING_LOOP:
        JSR CURCHAR
        BEQ FOR_SKIP_EOL
        CMP #'"'
        BEQ FOR_SKIP_STRING_END
        JSR ADV
        JMP FOR_SKIP_STRING_LOOP
FOR_SKIP_STRING_END:
        JSR ADV
        JMP FOR_SKIP_SCAN

FOR_SKIP_EOL:
        ; The end of the text of this line: its link says where the walk goes.
        LDY #0
        LDA (B_SCANLINE),Y
        PHA
        INY
        LDA (B_SCANLINE),Y
        STA B_SCANLINE+1
        PLA
        STA B_SCANLINE
        LDA B_SCANLINE
        ORA B_SCANLINE+1
        BEQ FOR_SKIP_NO_NEXT    ; the program stops first: there is no body
        LDA B_SCANLINE
        CLC
        ADC #4
        STA B_IPTR
        LDA B_SCANLINE+1
        ADC #0
        STA B_IPTR+1
        JMP FOR_SKIP_SCAN

FOR_SKIP_FOUND:
        ; B_IPTR is already past the NEXT and its variable, so the rest of that
        ; statement runs and the loop is behind us.
        LDA B_SCANLINE
        STA B_CURLINE
        LDA B_SCANLINE+1
        STA B_CURLINE+1
        RTS

FOR_SKIP_NO_NEXT:
        ; No NEXT anywhere: the program ends where it was scanning, which is
        ; where a FOR with no loop at all leaves the machine.
        LDA B_SCANLINE
        STA B_CURLINE
        LDA B_SCANLINE+1
        STA B_CURLINE+1
        RTS

; FORFRAME: B_TMPP points at the frame for level B_FORSP.
FORFRAME:
        LDA B_FORSP
        ASL A
        ASL A
        ASL A
        ASL A
        CLC
        ADC #<FORSTK
        STA B_TMPP
        LDA #>FORSTK
        ADC #0
        STA B_TMPP+1
        RTS

; FOR_VARSET: the loop variable takes FAC, using the name kept aside.
FOR_VARSET:
        LDA B_VNAME1
        STA B_TEMP
        LDA B_VNAME2
        STA B_TEMP2
        LDA B_VNAMET
        STA B_TEMP3
        JMP VARSET

DO_NEXT:
        ; An optional variable names the loop; without one it is the innermost.
        JSR SKIPSP
        JSR CURCHAR
        JSR UPPER
        CMP #'A'
        BCC NEXT_INNERMOST
        CMP #'Z'+1
        BCS NEXT_INNERMOST
        JSR PARSENAME
        BCS ERR_SYNTAX
        JMP NEXT_FIND
NEXT_INNERMOST:
        LDA #0
        STA B_TEMP
        STA B_TEMP2
NEXT_FIND:
        LDA B_FORSP
        BNE NEXT_HAVE_LOOP
        JMP ERR_NEXT_WITHOUT_FOR
NEXT_HAVE_LOOP:
        ; Frames are addressed by level, so the pointer is re-derived after
        ; every call that is free to move B_TMPP.
        JSR NEXTFRAME
        LDY #0
        LDA (B_TMPP),Y
        STA B_TEMP
        STA B_VNAME1
        INY
        LDA (B_TMPP),Y
        STA B_TEMP2
        STA B_VNAME2
        LDA #T_NUM
        STA B_TEMP3
        STA B_VNAMET
        JSR VARREF              ; B_FAC = the loop variable as it stands
        JSR NEXTFRAME
        LDY #7
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
        JSR FADD                ; B_FAC = the value after one more step
        JSR FOR_VARSET
        JSR NEXTFRAME
        LDY #2
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
        JSR FCOMP               ; A = -1 below, 0 level, 1 above
        STA B_CHAR
        JSR NEXTFRAME
        LDY #8                  ; bit 7 of the mantissa, not of the exponent
        LDA (B_TMPP),Y
        BMI NEXT_NEGATIVE
        LDA B_CHAR
        CMP #1
        BEQ NEXT_FINISHED
        JMP NEXT_LOOP
NEXT_NEGATIVE:
        LDA B_CHAR
        CMP #$FF
        BEQ NEXT_FINISHED
NEXT_LOOP:
        JSR NEXTFRAME
        LDY #12
        LDA (B_TMPP),Y
        STA B_CURLINE
        INY
        LDA (B_TMPP),Y
        STA B_CURLINE+1
        LDY #14
        LDA (B_TMPP),Y
        STA B_IPTR
        INY
        LDA (B_TMPP),Y
        STA B_IPTR+1
        LDA #0
        STA B_FLAGS
        SEC
        RTS
NEXT_FINISHED:
        DEC B_FORSP
        CLC
        RTS

; NEXTFRAME: B_TMPP is the frame of the loop NEXT is working on.
NEXTFRAME:
        LDA B_FORSP
        SEC
        SBC #1
        ASL A
        ASL A
        ASL A
        ASL A
        CLC
        ADC #<FORSTK
        STA B_TMPP
        LDA #>FORSTK
        ADC #0
        STA B_TMPP+1
        RTS

; --- INPUT -----------------------------------------------------------------

DO_INPUT:
        JSR SKIPSP
        JSR CURCHAR
        CMP #'"'
        BNE INPUT_VAR
        JSR ADV
INPUT_PROMPT_LOOP:
        JSR CURCHAR
        BEQ INPUT_ASK
        CMP #'"'
        BEQ INPUT_PROMPT_END
        JSR K_CHROUT
        JSR ADV
        JMP INPUT_PROMPT_LOOP
INPUT_PROMPT_END:
        JSR ADV
        JSR SKIPSP
        JSR CURCHAR
        CMP #';'
        BNE INPUT_ASK
        JSR ADV
        JMP INPUT_VAR
INPUT_ASK:
        LDA #'?'
        JSR K_CHROUT
INPUT_VAR:
        LDA #' '
        JSR K_CHROUT
        JSR PARSENAME
        BCS ERR_SYNTAX
        LDA B_TEMP
        STA B_VNAME1
        LDA B_TEMP2
        STA B_VNAME2
        LDA B_VNAMET
        STA B_VNAMET
        JSR K_GETLINE
        LDA #<INBUF
        STA B_IPTR
        LDA #>INBUF
        STA B_IPTR+1
        JSR FVAL
        BCS INPUT_REDO
        LDA B_VNAME1
        STA B_TEMP
        LDA B_VNAME2
        STA B_TEMP2
        LDA B_VNAMET
        STA B_TEMP3
        JSR VARSET
        ; more, separated by commas?
        JSR SKIPSP
        JSR CURCHAR
        CMP #','
        BNE INPUT_DONE
        JSR ADV
        JMP INPUT_VAR
INPUT_REDO:
        LDA #<TXT_REDO
        STA ZP_STRPTR
        LDA #>TXT_REDO
        STA ZP_STRPTR+1
        JSR K_PRINT
        JMP INPUT_ASK
INPUT_DONE:
        CLC
        RTS

; --- the simple ones -------------------------------------------------------

DO_REM:
DO_REM_LOOP:
        JSR CURCHAR
        BEQ DO_REM_DONE
        JSR ADV
        JMP DO_REM_LOOP
DO_REM_DONE:
        CLC
        RTS

DO_END:
        LDA #0
        STA B_CANCONT
        JMP BASIC_WARM

; STOP keeps enough of the interpreter's state for CONT to pick the program up
; again, and says where it stopped.
DO_STOP:
        LDA B_MODE
        BEQ DO_END              ; typed at the prompt, STOP just goes home
        LDA B_IPTR
        STA B_CONTPTR
        LDA B_IPTR+1
        STA B_CONTPTR+1
        LDA B_CURLINE
        STA B_CONTCUR
        LDA B_CURLINE+1
        STA B_CONTCUR+1
        LDA B_LINENO
        STA B_CONTLINE
        LDA B_LINENO+1
        STA B_CONTLINE+1
        LDA #1
        STA B_CANCONT
        LDA #<M_BREAK
        STA ZP_STRPTR
        LDA #>M_BREAK
        STA ZP_STRPTR+1
        JSR K_PRINT
        LDA #<TXT_IN
        STA ZP_STRPTR
        LDA #>TXT_IN
        STA ZP_STRPTR+1
        JSR K_PRINT
        LDA B_LINENO
        LDY B_LINENO+1
        JSR ITOF
        JSR FPRINT
        LDA #<B_NUMBUF
        STA ZP_STRPTR
        LDA #>B_NUMBUF
        STA ZP_STRPTR+1
        JSR K_PRINT
        LDA #CC_RETURN
        JSR K_CHROUT
        JMP BASIC_WARM

DO_CONT:
        LDA B_CANCONT
        BNE CONT_GO
        LDA #E_CANT_CONTINUE
        JMP ERROR
CONT_GO:
        LDA #0
        STA B_CANCONT           ; one go per stop
        LDA B_CONTPTR
        STA B_IPTR
        LDA B_CONTPTR+1
        STA B_IPTR+1
        LDA B_CONTCUR
        STA B_CURLINE
        LDA B_CONTCUR+1
        STA B_CURLINE+1
        LDA B_CONTLINE
        STA B_LINENO
        LDA B_CONTLINE+1
        STA B_LINENO+1
        LDA #1
        STA B_MODE
        JMP RUN_STMT

DO_CLS:
        JSR K_CLS
        CLC
        RTS

DO_POKE:
        JSR EXPR
        JSR FTOI
        BCS ERR_SYNTAX
        STA B_INT2              ; the address, kept clear of the second FTOI
        STY B_INT2+1
        JSR SKIPSP
        JSR CURCHAR
        CMP #','
        BNE ERR_SYNTAX
        JSR ADV
        JSR EXPR
        JSR FTOI
        BCS ERR_SYNTAX
        STA B_CHAR
        LDY #0
        LDA B_CHAR
        STA (B_INT2),Y
        CLC
        RTS

DO_LIST:
        JSR SKIPSP
        JSR CURCHAR
        BEQ LIST_ALL
        JSR PARSELINENO
        BCS LIST_ALL
        LDA B_INT               ; FINDLINE takes the number in A and Y
        LDY B_INT+1
        JSR FINDLINE
        BCS LIST_EMPTY
        LDA B_CURLINE
        STA B_LINEBUF
        LDA B_CURLINE+1
        STA B_LINEBUF+1
        JMP LIST_LOOP
LIST_ALL:
        JSR PROGEMPTY
        BCS LIST_EMPTY
        LDA B_TXTTAB
        STA B_LINEBUF
        LDA B_TXTTAB+1
        STA B_LINEBUF+1
LIST_LOOP:
        LDY #2
        LDA (B_LINEBUF),Y
        PHA
        INY
        LDA (B_LINEBUF),Y
        TAY
        PLA
        JSR ITOF
        JSR FPRINT
        LDA #<B_NUMBUF
        STA ZP_STRPTR
        LDA #>B_NUMBUF
        STA ZP_STRPTR+1
        JSR K_PRINT
        LDA #' '
        JSR K_CHROUT
        CLC
        LDA B_LINEBUF
        ADC #4
        STA ZP_STRPTR
        LDA B_LINEBUF+1
        ADC #0
        STA ZP_STRPTR+1
        JSR K_PRINT
        LDA #CC_RETURN
        JSR K_CHROUT
        LDY #0
        LDA (B_LINEBUF),Y
        PHA
        INY
        LDA (B_LINEBUF),Y
        STA B_LINEBUF+1
        PLA
        STA B_LINEBUF
        LDA B_LINEBUF
        ORA B_LINEBUF+1
        BEQ LIST_EMPTY
        JMP LIST_LOOP
LIST_EMPTY:
        CLC
        RTS

DO_NEW:
        JSR NEWPROG
        CLC
        RTS

DO_HELP:
        LDA #<TXT_HELP
        STA ZP_STRPTR
        LDA #>TXT_HELP
        STA ZP_STRPTR+1
        JSR K_PRINT
        CLC
        RTS

; ---------------------------------------------------------------------------
; Errors
; ---------------------------------------------------------------------------

ERR_SYNTAX:
        LDA #E_SYNTAX
        JMP ERROR
ERR_DIVISION_BY_ZERO:
        LDA #E_DIVISION_BY_ZERO
        JMP ERROR
ERR_OUT_OF_MEMORY:
        LDA #E_OUT_OF_MEMORY
        JMP ERROR
ERR_UNDEF_STATEMENT:
        LDA #E_UNDEF_STATEMENT
        JMP ERROR
ERR_UNDEF_LINE:
        LDA #E_UNDEF_STATEMENT
        JMP ERROR
ERR_RETURN:
        LDA #E_RETURN_WITHOUT_GOSUB
        JMP ERROR
ERR_NEXT_WITHOUT_FOR:
        LDA #E_NEXT_WITHOUT_FOR
        JMP ERROR
ERR_STRING_NOT_YET:
        LDA #E_TYPE_MISMATCH
        JMP ERROR
ERR_ARRAY_NOT_YET:
        LDA #E_UNDEF_STATEMENT
        JMP ERROR
ERR_ILLEGAL_QUANTITY:
        LDA #E_ILLEGAL_QUANTITY
        JMP ERROR
ERR_TYPE_MISMATCH:
        LDA #E_TYPE_MISMATCH
        JMP ERROR
ERR_BAD_SUBSCRIPT:
        LDA #E_BAD_SUBSCRIPT
        JMP ERROR
ERR_REDIMENSIONED:
        LDA #E_REDIM
        JMP ERROR
ERR_STRING_TOO_LONG:
        LDA #E_STRING_TOO_LONG
        JMP ERROR
ERR_NOT_YET:
        LDA #E_NOT_YET
        JMP ERROR
ERR_OUT_OF_DATA:
        LDA #E_OUT_OF_DATA
        JMP ERROR
ERR_BAD_DATA:
        LDA #E_BAD_DATA
        JMP ERROR
ERR_BAD_FILENAME:
        LDA #E_BAD_FILENAME
        JMP ERROR
ERR_DOS:
        LDA #E_DISK
        JMP ERROR
ERR_FILE_NOT_FOUND:
        LDA #E_FILE_NOT_FOUND
        JMP ERROR

ERROR:
        STA B_ERRNUM
        LDA #CC_RETURN
        JSR K_CHROUT
        LDA #'?'
        JSR K_CHROUT
        JSR PRINTERROR
        LDA #<TXT_ERROR
        STA ZP_STRPTR
        LDA #>TXT_ERROR
        STA ZP_STRPTR+1
        JSR K_PRINT
        ; In a program, the line number is part of the report.
        LDA B_MODE
        BEQ ERROR_NEWLINE
        LDA #<TXT_IN
        STA ZP_STRPTR
        LDA #>TXT_IN
        STA ZP_STRPTR+1
        JSR K_PRINT
        LDA B_LINENO
        LDY B_LINENO+1
        JSR ITOF
        JSR FPRINT
        LDA #<B_NUMBUF
        STA ZP_STRPTR
        LDA #>B_NUMBUF
        STA ZP_STRPTR+1
        JSR K_PRINT
ERROR_NEWLINE:
        LDA #CC_RETURN          ; the report ends its line either way
        JSR K_CHROUT
ERROR_DONE:
        JMP BASIC_WARM

PRINTERROR:
        LDA B_ERRNUM
        ASL A
        TAX
        LDA ERRTAB,X
        STA ZP_STRPTR
        LDA ERRTAB+1,X
        STA ZP_STRPTR+1
        JMP K_PRINT




; ---------------------------------------------------------------------------
; GET: one key, without waiting for Return
; ---------------------------------------------------------------------------

DO_GET:
        JSR PARSENAME
        BCS ERR_SYNTAX
        LDA B_TEMP3
        CMP #T_STR
        BEQ GET_NAMED
        JMP ERR_TYPE_MISMATCH
GET_NAMED:
        LDA B_TEMP
        STA B_VNAME1
        LDA B_TEMP2
        STA B_VNAME2
        LDA B_TEMP3
        STA B_VNAMET
GET_WAIT:
        JSR K_GETIN
        CMP #0
        BEQ GET_WAIT            ; the machine simply waits for a finger
        STA B_STRCHR
        LDA #<B_STRCHR
        STA B_STRPTR
        LDA #>B_STRCHR
        STA B_STRPTR+1
        LDA #1
        STA B_STRLEN
        JSR STRSET
        LDA B_VNAME1
        STA B_TEMP
        LDA B_VNAME2
        STA B_TEMP2
        LDA B_VNAMET
        STA B_TEMP3
        JMP STRASSIGN

; ---------------------------------------------------------------------------
; The disk
; ---------------------------------------------------------------------------
; LOAD and SAVE hand the program to Kestrel DOS, which lives in the KERNAL.

DO_SAVE:
        JSR PARSEFN
        BCS ERR_BAD_FILENAME
        LDA B_TXTTAB
        STA ZP_DSKPTR
        LDA B_TXTTAB+1
        STA ZP_DSKPTR+1
        LDA B_VARTAB
        STA ZP_DSKEND
        LDA B_VARTAB+1
        STA ZP_DSKEND+1
        JSR K_DOSSAVE
        BNE DOS_FAULT
        CLC
        RTS

DO_LOAD:
        JSR PARSEFN
        BCS ERR_BAD_FILENAME
        LDA B_TXTTAB
        STA ZP_DSKPTR
        LDA B_TXTTAB+1
        STA ZP_DSKPTR+1
        JSR K_DOSLOAD
        BNE DOS_FAULT
        ; what came back has to fit under the top of memory
        LDA ZP_DSKPTR
        CMP B_MEMTOP
        LDA ZP_DSKPTR+1
        SBC B_MEMTOP+1
        BCS ERR_OUT_OF_MEMORY
        LDA ZP_DSKPTR
        STA B_VARTAB
        LDA ZP_DSKPTR+1
        STA B_VARTAB+1
        JSR LINKPROG
        JSR CLEARVARS
        JSR RESET_STACKS
        LDA #0
        STA B_MODE
        JMP BASIC_WARM

DO_CATALOG:
        JSR K_DOSCATALOG
        CLC
        RTS

DOS_FAULT:
        CMP #ERR_NOT_FOUND      ; a file the directory does not list is named as
        BEQ ERR_FILE_NOT_FOUND  ; such; everything else is the disk's fault
        JMP ERR_DOS

; PARSEFN: the name in quotes after SAVE or LOAD goes to DOSNAME, upper case and
; padded with spaces. Carry set when there is no name.
PARSEFN:
        JSR SKIPSP
        JSR CURCHAR
        CMP #'"'
        BNE PARSEFN_FAIL
        JSR ADV
        LDX #0
PARSEFN_LOOP:
        JSR CURCHAR
        BEQ PARSEFN_SHUT
        CMP #'"'
        BEQ PARSEFN_CLOSE
        JSR UPPER
        CPX #8
        BCS PARSEFN_NEXT        ; a name is eight characters long
        STA DOSNAME,X
        INX
PARSEFN_NEXT:
        JSR ADV
        JMP PARSEFN_LOOP
PARSEFN_CLOSE:
        JSR ADV
PARSEFN_SHUT:
        CPX #8
        BCS PARSEFN_OK
        LDA #' '
PARSEFN_PAD:
        STA DOSNAME,X
        INX
        CPX #8
        BCC PARSEFN_PAD
PARSEFN_OK:
        LDA DOSNAME             ; a name of nothing but spaces is no name
        CMP #' '
        BNE PARSEFN_GOOD
        SEC
        RTS
PARSEFN_GOOD:
        CLC
        RTS
PARSEFN_FAIL:
        SEC
        RTS

; ---------------------------------------------------------------------------
; ON ... GOTO / GOSUB
; ---------------------------------------------------------------------------
; The index picks one line number out of the list; anything outside it simply
; runs on with the next statement, as it did on the machines of the day.

DO_ON:
        JSR EXPR
        JSR FTOI
        BCS ERR_ILLEGAL_QUANTITY
        STA B_INT2
        STY B_INT2+1
        JSR SKIPSP
        LDA #0
        STA B_ONFLAG
        LDA #<KW_GOTO
        LDY #>KW_GOTO
        JSR TRY
        BCS ON_HAVE_WORD
        LDA #1
        STA B_ONFLAG
        LDA #<KW_GOSUB
        LDY #>KW_GOSUB
        JSR TRY
        BCS ON_HAVE_WORD
        JMP ERR_SYNTAX
ON_HAVE_WORD:
        LDA B_INT2
        ORA B_INT2+1
        BNE ON_CHOOSE
        CLC                     ; a zero index falls through
        RTS
ON_CHOOSE:
        LDA B_INT2+1
        BNE ON_FALL_THROUGH     ; a negative or huge index never matches
        DEC B_INT2              ; the first number is index one
ON_LIST:
        JSR SKIPSP
        JSR PARSELINENO
        BCS ERR_SYNTAX
        LDA B_INT2
        BNE ON_NEXT
        JMP ON_JUMP
ON_NEXT:
        DEC B_INT2
        JSR SKIPSP
        JSR CURCHAR
        CMP #','
        BNE ON_FALL_THROUGH
        JSR ADV
        JMP ON_LIST
ON_FALL_THROUGH:
        ; step over the whole list and carry on
ON_SKIP:
        JSR CURCHAR
        BEQ ON_SKIPPED
        CMP #':'
        BEQ ON_SKIPPED
        JSR ADV
        JMP ON_SKIP
ON_SKIPPED:
        CLC
        RTS
ON_JUMP:
        JSR SKIPSP
        JSR CURCHAR
        CMP #','
        BEQ ON_JUMP_OK
        CMP #0
        BEQ ON_JUMP_OK
        CMP #':'
        BEQ ON_JUMP_OK
        JMP ERR_SYNTAX
ON_JUMP_OK:
        LDA B_ONFLAG
        BEQ ON_JUMP_GO
        ; a subroutine returns to the statement after this one
        LDX B_GOSUBSP
        CPX #GOSUB_MAX
        BCS ERR_OUT_OF_MEMORY
        TXA
        ASL A
        ASL A
        TAX
        LDA B_CURLINE
        STA GOSUBSTK,X
        LDA B_CURLINE+1
        STA GOSUBSTK+1,X
        LDA B_IPTR
        STA GOSUBSTK+2,X
        LDA B_IPTR+1
        STA GOSUBSTK+3,X
        INC B_GOSUBSP
ON_JUMP_GO:
        LDA B_INT
        LDY B_INT+1
        JSR FINDLINE
        BCS ERR_UNDEF_LINE
        LDA #1
        STA B_FLAGS
        SEC
        RTS

; ---------------------------------------------------------------------------
; DELETE
; ---------------------------------------------------------------------------
; The program is squeezed in place: every line outside the range is copied
; down over the ones inside it.

DO_DELETE:
        JSR SKIPSP
        JSR PARSELINENO
        BCS ERR_SYNTAX
        LDA B_INT
        STA B_INT2              ; the first line of the range
        LDA B_INT+1
        STA B_INT2+1
        JSR SKIPSP
        JSR CURCHAR
        CMP #','
        BNE DELETE_ONE          ; a single number means that one line only
        JSR ADV
        JSR SKIPSP
        JSR PARSELINENO
        BCS ERR_SYNTAX
        JMP DELETE_GO
DELETE_ONE:
        LDA B_INT
        STA B_INT
        LDA B_INT+1
        STA B_INT+1
DELETE_GO:
        ; B_INT holds the last line of the range, B_INT2 the first. The test
        ; above the range is against one past its top, so that the last line is
        ; dropped too.
        LDA B_INT
        CLC
        ADC #1
        STA B_TEMP
        LDA B_INT+1
        ADC #0
        STA B_TEMP2
        LDA B_TXTTAB
        STA B_LINEBUF
        LDA B_TXTTAB+1
        STA B_LINEBUF+1
        LDA B_TXTTAB
        STA B_TMPP2
        LDA B_TXTTAB+1
        STA B_TMPP2+1
DELETE_LOOP:
        LDA B_LINEBUF
        CMP B_VARTAB
        LDA B_LINEBUF+1
        SBC B_VARTAB+1
        BCS DELETE_DONE
        LDY #2
        LDA (B_LINEBUF),Y
        CMP B_INT2
        INY
        LDA (B_LINEBUF),Y
        SBC B_INT2+1
        BCC DELETE_KEEP         ; below the range
        LDY #2
        LDA (B_LINEBUF),Y
        CMP B_TEMP
        INY
        LDA (B_LINEBUF),Y
        SBC B_TEMP2
        BCC DELETE_DROP         ; inside the range
DELETE_KEEP:
        JSR STORE_COPY_LINE
        JSR STORE_ADVANCE
        LDA B_LINEBUF
        ORA B_LINEBUF+1
        BEQ DELETE_DONE
        JMP DELETE_LOOP
DELETE_DROP:
        JSR STORE_ADVANCE
        LDA B_LINEBUF
        ORA B_LINEBUF+1
        BEQ DELETE_DONE
        JMP DELETE_LOOP
DELETE_DONE:
        LDA B_TMPP2
        STA B_VARTAB
        LDA B_TMPP2+1
        STA B_VARTAB+1
        JSR LINKPROG
        JMP BASIC_WARM

; ---------------------------------------------------------------------------
; READ and DATA
; ---------------------------------------------------------------------------
; DATA is not a statement at all: DO_REM steps over it and the values stay in
; the program's own text, which is where READ looks for them.

DO_RESTORE:
        LDA #0
        STA B_DATALINE
        STA B_DATALINE+1
        STA B_DATAPTR
        STA B_DATAPTR+1
        LDA #0
        STA B_DATFLAG
        CLC
        RTS

DO_READ:
        LDA B_IPTR
        STA B_LINEBUF
        LDA B_IPTR+1
        STA B_LINEBUF+1
READ_LOOP:
        LDA B_LINEBUF
        STA B_IPTR
        LDA B_LINEBUF+1
        STA B_IPTR+1
        JSR PARSENAME
        BCS ERR_SYNTAX
        LDA B_TEMP
        STA B_VNAME1
        LDA B_TEMP2
        STA B_VNAME2
        LDA B_TEMP3
        STA B_VNAMET
        JSR SKIPSP
        JSR CURCHAR
        CMP #'('
        BEQ READ_ELEMENT
        LDA B_IPTR
        STA B_LINEBUF
        LDA B_IPTR+1
        STA B_LINEBUF+1
        JSR READVALUE
        LDA B_VNAME1
        STA B_TEMP
        LDA B_VNAME2
        STA B_TEMP2
        LDA B_VNAMET
        STA B_TEMP3
        LDA B_VNAMET
        CMP #T_STR
        BEQ READ_TAKE_STRING
        JSR STRNONE
        JSR VARSET
        JMP READ_NEXT_IN_LIST
READ_TAKE_STRING:
        JSR STRASSIGN
        JMP READ_NEXT_IN_LIST
READ_ELEMENT:
        ; the name is still in B_TEMP.. and the subscripts are next
        JSR ARY_ADDR
        LDA B_ARYTYPE
        PHA
        LDA B_TMPP
        PHA
        LDA B_TMPP+1
        PHA
        LDA B_IPTR
        STA B_LINEBUF
        LDA B_IPTR+1
        STA B_LINEBUF+1
        JSR READVALUE
        PLA
        STA B_TMPP+1
        PLA
        STA B_TMPP
        PLA
        BPL READ_ELEMENT_NUMBER
        JSR ARY_STORE_STRING
        JMP READ_NEXT_IN_LIST
READ_ELEMENT_NUMBER:
        JSR STRNONE
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
READ_NEXT_IN_LIST:
        LDA B_LINEBUF
        STA B_IPTR
        LDA B_LINEBUF+1
        STA B_IPTR+1
        JSR SKIPSP
        JSR CURCHAR
        CMP #','
        BNE READ_DONE
        JSR ADV
        LDA B_IPTR
        STA B_LINEBUF
        LDA B_IPTR+1
        STA B_LINEBUF+1
        JMP READ_LOOP
READ_DONE:
        LDA B_LINEBUF
        STA B_IPTR
        LDA B_LINEBUF+1
        STA B_IPTR+1
        CLC
        RTS


; READVALUE: FAC = the next value in the DATA statements.
READVALUE:
        LDA B_DATFLAG
        BNE READV_ITEM
        JSR SEARCHDATA
        BCS READV_FOUND
        JMP ERR_OUT_OF_DATA
READV_FOUND:
        LDA #1
        STA B_DATFLAG
READV_ITEM:
        LDA B_DATAPTR
        STA B_IPTR
        LDA B_DATAPTR+1
        STA B_IPTR+1
        JSR SKIPSP
        JSR CURCHAR
        CMP #'"'
        BEQ READV_STRING
        JSR FVAL
        BCS READV_BAD
        JSR STRCLR
        JMP READV_AFTER
READV_STRING:
        JSR ADV                 ; past the opening quote
        LDA B_IPTR
        STA B_STRPTR
        LDA B_IPTR+1
        STA B_STRPTR+1
        LDA #0
        STA B_STRLEN
READV_STR_LOOP:
        JSR CURCHAR
        BEQ READV_STR_END       ; an unterminated string ends the line
        CMP #'"'
        BEQ READV_STR_SHUT
        INC B_STRLEN
        JSR ADV
        JMP READV_STR_LOOP
READV_STR_SHUT:
        JSR ADV
READV_STR_END:
        JSR STRSET
READV_AFTER:
        JSR SKIPSP
        JSR CURCHAR
        CMP #','
        BEQ READV_COMMA
        CMP #0
        BNE READV_BAD
        ; The list has run out. The value just read is the answer; the search
        ; for more data waits until a READ actually needs it.
        LDA #0
        STA B_DATFLAG
        JSR DATA_NEXTLINE
        CLC
        RTS
READV_COMMA:
        JSR ADV
        LDA B_IPTR
        STA B_DATAPTR
        LDA B_IPTR+1
        STA B_DATAPTR+1
        CLC
        RTS
READV_BAD:
        JMP ERR_BAD_DATA

; SEARCHDATA: from the line at B_DATALINE, find the first DATA statement and
; leave B_DATAPTR at its first value. Carry set when there is one.
SEARCHDATA:
        LDA B_DATALINE
        ORA B_DATALINE+1
        BNE SD_LINE
        LDA B_TXTTAB
        STA B_DATALINE
        LDA B_TXTTAB+1
        STA B_DATALINE+1
SD_LINE:
        LDA B_DATALINE
        CMP B_VARTAB
        LDA B_DATALINE+1
        SBC B_VARTAB+1
        BCS SD_NONE
        LDA B_DATALINE
        CLC
        ADC #4
        STA B_IPTR
        LDA B_DATALINE+1
        ADC #0
        STA B_IPTR+1
SD_STMT:
        LDA #<KW_DATA
        LDY #>KW_DATA
        JSR TRY
        BCS SD_FOUND
SD_SKIP:
        LDY #0
        LDA (B_IPTR),Y
        BEQ SD_NEXT
        CMP #':'
        BEQ SD_COLON
        CMP #'"'
        BEQ SD_QUOTE_IN
        JSR ADV
        JMP SD_SKIP
SD_COLON:
        JSR ADV
        JMP SD_STMT
SD_QUOTE_IN:
        JSR ADV
SD_QUOTE_LOOP:
        JSR CURCHAR
        BEQ SD_NEXT             ; an unterminated string ends the line
        JSR ADV
        CMP #'"'
        BNE SD_QUOTE_LOOP
        JMP SD_SKIP
SD_NEXT:
        JSR DATA_NEXTLINE
        BCC SD_NONE
        JMP SD_LINE
SD_NONE:
        CLC
        RTS
SD_FOUND:
        LDA B_IPTR
        STA B_DATAPTR
        LDA B_IPTR+1
        STA B_DATAPTR+1
        SEC
        RTS

; DATA_NEXTLINE: B_DATALINE follows its line's link. Carry clear at the end of
; the program.
DATA_NEXTLINE:
        LDY #0
        LDA (B_DATALINE),Y
        STA B_TMP
        INY
        LDA (B_DATALINE),Y
        STA B_TMP+1
        LDA B_TMP
        ORA B_TMP+1
        BEQ DATA_NEXTLINE_END
        LDA B_TMP
        STA B_DATALINE
        LDA B_TMP+1
        STA B_DATALINE+1
        SEC
        RTS
DATA_NEXTLINE_END:
        ; Past the last line there is no data at all, so park the cursor where
        ; the next search will see the end of the program at once.
        LDA B_VARTAB
        STA B_DATALINE
        LDA B_VARTAB+1
        STA B_DATALINE+1
        CLC
        RTS

; ---------------------------------------------------------------------------
; Words and text
; ---------------------------------------------------------------------------

KW_PRINT:  .asciiz "PRINT"
KW_LET:    .asciiz "LET"
KW_IF:     .asciiz "IF"
KW_THEN:   .asciiz "THEN"
KW_ELSE:   .asciiz "ELSE"
KW_GOTO:   .asciiz "GOTO"
KW_GOSUB:  .asciiz "GOSUB"
KW_RETURN: .asciiz "RETURN"
KW_FOR:    .asciiz "FOR"
KW_NEXT:   .asciiz "NEXT"
KW_TO:     .asciiz "TO"
KW_STEP:   .asciiz "STEP"
KW_INPUT:  .asciiz "INPUT"
KW_REM:    .asciiz "REM"
KW_END:    .asciiz "END"
KW_STOP:   .asciiz "STOP"
KW_DATA:   .asciiz "DATA"
KW_READ:   .asciiz "READ"
KW_RESTORE: .asciiz "RESTORE"
KW_GET:    .asciiz "GET"
KW_DIM:    .asciiz "DIM"
KW_CONT:   .asciiz "CONT"
KW_DEF:    .asciiz "DEF"
KW_FN:     .asciiz "FN"
KW_ON:     .asciiz "ON"
KW_DELETE: .asciiz "DELETE"
KW_LOAD:   .asciiz "LOAD"
KW_SAVE:   .asciiz "SAVE"
KW_CATALOG: .asciiz "CATALOG"
KW_CLS:    .asciiz "CLS"
KW_POKE:   .asciiz "POKE"
KW_LIST:   .asciiz "LIST"
KW_RUN:    .asciiz "RUN"
KW_NEW:    .asciiz "NEW"
KW_HELP:   .asciiz "HELP"

TXT_BANNER:
        .byte CC_CLS
        .text "*** KESTREL BASIC V2.0 ROM ***"
        .byte 13
        .text "64K RAM SYSTEM"
        .byte 13
        .text "(C) 1982 HONEYBADGER SYSTEMS"
        .byte 13, 13, 0

TXT_READY:
        .text "READY."
        .byte 13, 0

TXT_ERROR:
        .text " ERROR"
        .byte 0

TXT_IN:
        .text " IN "
        .byte 0

TXT_REDO:
        .text "?REDO FROM START"
        .byte 13, 0

TXT_HELP:
        .text "KESTREL BASIC V2.0 COMMANDS"
        .byte 13
        .text "PROGRAM"
        .byte 13
        .text "  LIST [n]  RUN  NEW  DELETE n[,m]"
        .byte 13
        .text "  SAVE "
        .byte 34
        .text "NAME"
        .byte 34
        .text "  LOAD "
        .byte 34
        .text "NAME"
        .byte 34
        .text "  CATALOG"
        .byte 13, 13
        .text "STATEMENTS"
        .byte 13
        .text "  PRINT ? LET DIM INPUT GET READ"
        .byte 13
        .text "  DATA RESTORE IF..THEN..ELSE"
        .byte 13
        .text "  FOR..TO..STEP..NEXT"
        .byte 13
        .text "  GOTO GOSUB RETURN ON..GOTO"
        .byte 13
        .text "  REM END STOP CONT POKE CLS"
        .byte 13, 13
        .text "FUNCTIONS"
        .byte 13
        .text "  ABS INT SGN SQR RND PEEK"
        .byte 13
        .text "  SIN COS TAN ATN EXP LOG"
        .byte 13
        .text "  LEN ASC CHR$ STR$ VAL"
        .byte 13
        .text "  LEFT$ RIGHT$ MID$"
        .byte 13, 13
        .text "EXPRESSIONS"
        .byte 13
        .text "  + - * / ^ ( ) unary -  AND OR NOT"
        .byte 13
        .text "  + joins two strings, and strings"
        .byte 13
        .text "  compare with = <> < > <= >="
        .byte 13
        .text "  = <> < > <= >=  1  2.5  -3  1E3"
        .byte 13, 13
        .text "VARIABLES  A..Z  A0..Z9  A$..Z9$"
        .byte 13, 13
        .text "ARRAYS  A(n)  M(rows,cols)  DIM A$(n)"
        .byte 13
        .text "  four dimensions at most"
        .byte 13, 13
        .text "FUNCTIONS OF YOUR OWN"
        .byte 13
        .text "  DEF FN A(X)=X*X+1   PRINT FN A(3)"
        .byte 13, 13
        .text "TAPE  the monitor ROM has S and L"
        .byte 13, 13, 0

ERRTAB:
        .word M_NONE
        .word M_NEXT
        .word M_SYNTAX
        .word M_RETURN
        .word M_DATA
        .word M_IF
        .word M_REDIM
        .word M_DIVZERO
        .word M_MEMORY
        .word M_STATEMENT
        .word M_SUBSCRIPT
        .word M_QUANTITY
        .word M_FILENAME
        .word M_NOTFOUND
        .word M_DISK
        .word M_BREAK
        .word M_OVERFLOW
        .word M_FUNCTION
        .word M_BADDATA
        .word M_TOOLONG
        .word M_CONTINUE
        .word M_NOFILE
        .word M_NOTYET
        .word M_TYPEMISMATCH

M_NONE:      .asciiz "OK"
M_NEXT:      .asciiz "NEXT WITHOUT FOR"
M_SYNTAX:    .asciiz "SYNTAX"
M_RETURN:    .asciiz "RETURN WITHOUT GOSUB"
M_DATA:      .asciiz "OUT OF DATA"
M_IF:        .asciiz "IF WITHOUT THEN"
M_REDIM:     .asciiz "REDIMENSIONED ARRAY"
M_DIVZERO:   .asciiz "DIVISION BY ZERO"
M_MEMORY:    .asciiz "OUT OF MEMORY"
M_STATEMENT: .asciiz "UNDEF'D STATEMENT"
M_SUBSCRIPT: .asciiz "BAD SUBSCRIPT"
M_QUANTITY:  .asciiz "ILLEGAL QUANTITY"
M_FILENAME:  .asciiz "BAD FILE NAME"
M_NOTFOUND:  .asciiz "FILE NOT FOUND"
M_DISK:      .asciiz "DISK ERROR"
M_BREAK:     .asciiz "BREAK"
M_OVERFLOW:  .asciiz "OVERFLOW"
M_FUNCTION:  .asciiz "UNDEF'D FUNCTION"
M_BADDATA:   .asciiz "BAD DATA"
M_TOOLONG:   .asciiz "STRING TOO LONG"
M_CONTINUE:  .asciiz "CAN'T CONTINUE"
M_NOFILE:    .asciiz "NO FILE"
M_NOTYET:    .asciiz "NOT IN THIS ROM YET"
M_TYPEMISMATCH: .asciiz "TYPE MISMATCH"

        .include "def.s"
        .include "ary.s"
        .include "str.s"
        .include "func.s"
        .include "fp.s"
        .include "num.s"
        .include "eval.s"
