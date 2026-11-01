; SPDX-License-Identifier: MIT
;
; kernal.s -- the HB-1's KERNAL: the screen, the keyboard, the frame, the disk.
;
; This is the part of the machine that is always there. It knows how to put a
; character on the tube, how to read a line, how to move a sector, and how to
; keep a directory. It knows nothing about any language: at the end of its
; reset it jumps to the ROM in the socket, at $A000, and from then on the
; language owns the machine and the KERNAL is a set of services it calls.
;
; Every entry point is a fixed address in the jump table below. BASIC calls
; them through those addresses; so would a Logo or a Forth, and so does a
; machine-code program a person writes with POKE.

        .include "hb1.inc"

        .org KERNAL

        jmp CHROUT              ; $E000
        jmp GETIN               ; $E003
        jmp CHRIN               ; $E006
        jmp CLS                 ; $E009
        jmp HOME                ; $E00C
        jmp PRINT               ; $E00F
        jmp SCROLL              ; $E012
        jmp LOCATE              ; $E015
        jmp GETLINE             ; $E018
        jmp READSECT            ; $E01B
        jmp WRITESECT           ; $E01E
        jmp CURSORON            ; $E021
        jmp DOSCATALOG          ; $E024
        jmp DOSFIND             ; $E027
        jmp DOSLOAD             ; $E02A
        jmp DOSSAVE             ; $E02D
        jmp DOSDELETE           ; $E030
        jmp DOSSTATUS           ; $E033
        jmp DOSFORMAT           ; $E036

; ---------------------------------------------------------------------------
; Reset, break and the frame
; ---------------------------------------------------------------------------

        .org KERNAL+$40

RESET:
        SEI
        CLD
        LDX #$FF
        TXS
        LDA #0
        STA IO_FRAMEFLAG
        STA IO_CURSOR
        STA ZP_CURSOR
        STA ZP_BLINK
        STA ZP_IRQCNT
        STA ZP_KBHEAD
        STA ZP_KBTAIL
        STA ZP_KBCOUNT
        LDA #LIGHTBLUE
        STA IO_BORDER
        LDA #LIGHTGREY
        STA IO_INK
        LDA #BLACK
        STA IO_PAPER
        JSR CLS
        CLI
        JMP COLD_START

; The frame interrupt. It does three things and nothing else: counts frames,
; blinks the cursor, and empties the keyboard controller into the KERNAL's own
; buffer -- which it must do, because the controller's interrupt line stays
; high until its last key is read.
IRQ:
        PHA
        TXA
        PHA
        TYA
        PHA
        LDA IO_FRAMEFLAG
        BEQ IRQ_KEYS
        LDA #0
        STA IO_FRAMEFLAG
        ; The tube draws the cursor from its own registers, so the frame is
        ; where the KERNAL tells it where the cursor has got to.
        LDA ZP_COL
        STA IO_CURCOL
        LDA ZP_ROW
        STA IO_CURROW
        INC ZP_IRQCNT
        LDA ZP_IRQCNT
        AND #31
        BNE IRQ_KEYS
        LDA IO_CURSOR
        EOR #1
        STA IO_CURSOR
IRQ_KEYS:
        LDA IO_KBDSTAT
        BEQ IRQ_DONE
        LDA IO_KBDDATA
        JSR KBPUSH
        JMP IRQ_KEYS
IRQ_DONE:
        PLA
        TAY
        PLA
        TAX
        PLA
        RTI

; BREAK: a warm start. The ROM in the socket decides what that means.
NMI:
        JMP WARM_START

; ---------------------------------------------------------------------------
; The keyboard
; ---------------------------------------------------------------------------

; KBPUSH: A = key. Drops a key when the buffer is full, which is what a
; machine with sixteen places would do rather than overwrite what is waiting.
KBPUSH:
        LDX ZP_KBCOUNT
        CPX #KEYBUF_MASK
        BCS KBPUSH_DONE
        LDY ZP_KBHEAD
        STA KEYBUF,Y
        INY
        TYA
        AND #KEYBUF_MASK
        STA ZP_KBHEAD
        INC ZP_KBCOUNT
KBPUSH_DONE:
        RTS

; GETIN: -> A = the next key, or 0. X and Y preserved.
GETIN:
        TYA
        PHA
        LDA ZP_KBCOUNT
        BEQ GETIN_NONE
        LDY ZP_KBTAIL
        LDA KEYBUF,Y
        STA ZP_CHR              ; held while Y goes back where it was
        INY
        TYA
        AND #KEYBUF_MASK
        STA ZP_KBTAIL
        DEC ZP_KBCOUNT
        PLA
        TAY
        LDA ZP_CHR
        RTS
GETIN_NONE:
        PLA
        TAY
        LDA #0
        RTS

; CHRIN: waits for a key.
CHRIN:
        JSR GETIN
        BEQ CHRIN
        RTS

; GETLINE: reads a line into INBUF, echoing it, with Rub Out as the only editor.
; A 1982 machine at this price was a teletype: you typed the line, and if you
; wanted a different one you typed it again.
GETLINE:
        LDA #0
        STA ZP_INLEN
        LDA #1
        JSR CURSORON
GETLINE_KEY:
        JSR CHRIN
        CMP #CC_RETURN
        BEQ GETLINE_END
        CMP #CC_BACKSPACE
        BEQ GETLINE_RUB
        CMP #127
        BEQ GETLINE_RUB
        CMP #27
        BEQ GETLINE_ABORT
        CMP #32
        BCC GETLINE_KEY
        CMP #127
        BCS GETLINE_KEY
        LDY ZP_INLEN
        CPY #INBUF_MAX
        BCS GETLINE_KEY
        STA INBUF,Y
        INC ZP_INLEN
        JSR CHROUT
        JMP GETLINE_KEY
GETLINE_RUB:
        LDA ZP_INLEN
        BEQ GETLINE_KEY
        DEC ZP_INLEN
        JSR BACKSPACE
        JMP GETLINE_KEY
GETLINE_ABORT:
        LDA #0
        STA ZP_INLEN
GETLINE_END:
        ; Terminate the line: everything that reads INBUF afterwards is looking
        ; for the zero, and the buffer is reused for the next line.
        LDY ZP_INLEN
        LDA #0
        STA INBUF,Y
        LDA #0
        JSR CURSORON
        JSR NEWLINE
        RTS

; CURSORON: A = 1 for a solid cursor, 0 for none.
CURSORON:
        STA ZP_CURSOR
        STA IO_CURSOR
        RTS

; ---------------------------------------------------------------------------
; The screen
; ---------------------------------------------------------------------------

; CHROUT: A = character. X and Y are preserved; A comes back unchanged, so a
; caller can print in a loop without reloading it.
CHROUT:
        STA ZP_CHR
        TYA
        PHA
        TXA
        PHA
        LDA ZP_CHR
        JSR CHROUT_INNER
        PLA
        TAX
        PLA
        TAY
        LDA ZP_CHR
        RTS

CHROUT_INNER:
        CMP #CC_CLS
        BEQ CHROUT_TO_CLS
        CMP #CC_HOME
        BEQ CHROUT_TO_HOME
        CMP #CC_RETURN
        BEQ CHROUT_TO_NEWLINE
        CMP #CC_BACKSPACE
        BEQ CHROUT_TO_BACKSPACE
        CMP #32
        BCC CHROUT_COLOUR
        CMP #127
        BCS CHROUT_IGNORE
        JMP PUTCHAR
CHROUT_TO_CLS:
        JMP CLS
CHROUT_TO_HOME:
        JMP HOME
CHROUT_TO_NEWLINE:
        JMP NEWLINE
CHROUT_TO_BACKSPACE:
        JMP BACKSPACE
CHROUT_COLOUR:
        CMP #1
        BCC CHROUT_IGNORE
        CMP #16
        BCS CHROUT_IGNORE
        STA IO_INK
CHROUT_IGNORE:
        RTS

; HOME: cursor to the top left, screen untouched.
HOME:
        LDA #0
        STA ZP_COL
        STA ZP_ROW
        RTS

; CLS: a screen of spaces in the current colours, cursor home.
CLS:
        LDX #0
        LDA IO_INK
        ASL A
        ASL A
        ASL A
        ASL A
        ORA IO_PAPER
        STA ZP_TMP
CLS_LOOP:
        LDA #32
        STA SCREEN,X
        STA SCREEN+250,X
        STA SCREEN+500,X
        STA SCREEN+750,X
        LDA ZP_TMP
        STA ATTRS,X
        STA ATTRS+250,X
        STA ATTRS+500,X
        STA ATTRS+750,X
        INX
        CPX #250
        BNE CLS_LOOP
        LDA #0
        STA ZP_COL
        STA ZP_ROW
        RTS

; SETPTR: ZP_PTR = (ZP_TMP3/ZP_TMP4) + row*40 + column.
SETPTR:
        LDA ZP_ROW
        ASL A
        TAY
        LDA ROWOFF,Y
        CLC
        ADC ZP_TMP3
        STA ZP_PTR
        LDA ROWOFF+1,Y
        ADC ZP_TMP4
        STA ZP_PTR+1
        LDA ZP_COL
        CLC
        ADC ZP_PTR
        STA ZP_PTR
        BCC SETPTR_DONE
        INC ZP_PTR+1
SETPTR_DONE:
        RTS

; PUTCHAR: A = character at the cursor, then advance.
PUTCHAR:
        JSR PUTCHAR_AT
        JMP NEXTCOL

; PUTCHAR_AT: A = character at the cursor, cursor unmoved.
PUTCHAR_AT:
        STA ZP_CHR
        LDA #<SCREEN
        STA ZP_TMP3
        LDA #>SCREEN
        STA ZP_TMP4
        JSR SETPTR
        LDY #0
        LDA ZP_CHR
        STA (ZP_PTR),Y
        LDA IO_INK
        ASL A
        ASL A
        ASL A
        ASL A
        ORA IO_PAPER
        STA ZP_TMP2
        LDA #<ATTRS
        STA ZP_TMP3
        LDA #>ATTRS
        STA ZP_TMP4
        JSR SETPTR
        LDY #0
        LDA ZP_TMP2
        STA (ZP_PTR),Y
        RTS

NEXTCOL:
        INC ZP_COL
        LDA ZP_COL
        CMP #COLS
        BCC NEXTCOL_DONE
        JMP NEWLINE
NEXTCOL_DONE:
        RTS

NEWLINE:
        LDA #0
        STA ZP_COL
        INC ZP_ROW
        LDA ZP_ROW
        CMP #ROWS
        BCC NEWLINE_DONE
        JSR SCROLL
        LDA #ROWS-1
        STA ZP_ROW
NEWLINE_DONE:
        RTS

; BACKSPACE: erase the cell to the left and stand on it.
BACKSPACE:
        LDA ZP_COL
        BNE BACKSPACE_LEFT
        LDA ZP_ROW
        BEQ BACKSPACE_DONE
        DEC ZP_ROW
        LDA #COLS
BACKSPACE_LEFT:
        SEC
        SBC #1
        STA ZP_COL
        LDA #32
        JSR PUTCHAR_AT
BACKSPACE_DONE:
        RTS

; SCROLL: every row moves up one, the bottom row becomes spaces, and the
; machine records that it happened so a program watching the screen can tell.
;
; It uses ZP_INPTR and ZP_PTR2, never ZP_STRPTR: a scroll can happen in the
; middle of PRINT, and the string being printed lives in ZP_STRPTR.
SCROLL:
        LDA #<SCREEN
        STA ZP_PTR2
        LDA #>SCREEN
        STA ZP_PTR2+1
        LDA #<(SCREEN+COLS)
        STA ZP_INPTR
        LDA #>(SCREEN+COLS)
        STA ZP_INPTR+1
        JSR BLOCKMOVE
        LDA #<ATTRS
        STA ZP_PTR2
        LDA #>ATTRS
        STA ZP_PTR2+1
        LDA #<(ATTRS+COLS)
        STA ZP_INPTR
        LDA #>(ATTRS+COLS)
        STA ZP_INPTR+1
        JSR BLOCKMOVE
        LDA IO_INK
        ASL A
        ASL A
        ASL A
        ASL A
        ORA IO_PAPER
        STA ZP_TMP
        LDX #0
SCROLL_CLEAR:
        LDA #32
        STA SCREEN+960,X
        LDA ZP_TMP
        STA ATTRS+960,X
        INX
        CPX #COLS
        BNE SCROLL_CLEAR
        LDA #$FF
        STA IO_SCROLLED
        RTS

; BLOCKMOVE: 960 bytes from (ZP_INPTR) to (ZP_PTR2), rising.
BLOCKMOVE:
        LDA #<$03C0
        STA ZP_TMP
        LDA #>$03C0
        STA ZP_TMP2
BLOCKMOVE_LOOP:
        LDY #0
        LDA (ZP_INPTR),Y
        STA (ZP_PTR2),Y
        INC ZP_INPTR
        BNE BLOCKMOVE_SRC
        INC ZP_INPTR+1
BLOCKMOVE_SRC:
        INC ZP_PTR2
        BNE BLOCKMOVE_TICK
        INC ZP_PTR2+1
BLOCKMOVE_TICK:
        LDA ZP_TMP
        BNE BLOCKMOVE_DEC
        DEC ZP_TMP2
BLOCKMOVE_DEC:
        DEC ZP_TMP
        LDA ZP_TMP
        ORA ZP_TMP2
        BNE BLOCKMOVE_LOOP
        RTS

; LOCATE: X = column, Y = row.
LOCATE:
        STX ZP_COL
        STY ZP_ROW
        RTS

; PRINT: the zero-terminated string at ZP_STRPTR. The pointer walks, rather
; than an index counting: a machine that stops after 256 characters cannot
; print a help page.
PRINT:
        LDY #0
PRINT_LOOP:
        LDA (ZP_STRPTR),Y
        BEQ PRINT_DONE
        JSR CHROUT
        INC ZP_STRPTR
        BNE PRINT_LOOP
        INC ZP_STRPTR+1
        JMP PRINT_LOOP
PRINT_DONE:
        RTS

; ---------------------------------------------------------------------------
; The disk controller
; ---------------------------------------------------------------------------

; READSECT: A = sector low, X = sector high, ZP_PTR = where the 256 bytes go.
; -> A = 0, or a nonzero error code.
READSECT:
        STA IO_DSKL
        STX IO_DSKH
        LDA ZP_PTR
        STA IO_DBUFL
        LDA ZP_PTR+1
        STA IO_DBUFH
        LDA #1
        STA IO_DCMD
        LDA IO_DSTAT
        BEQ READSECT_OK
        LDA #ERR_DISK
        RTS
READSECT_OK:
        LDA #0
        RTS

; WRITESECT: A = sector low, X = sector high, ZP_PTR = the 256 bytes to write.
WRITESECT:
        STA IO_DSKL
        STX IO_DSKH
        LDA ZP_PTR
        STA IO_DBUFL
        LDA ZP_PTR+1
        STA IO_DBUFH
        LDA #2
        STA IO_DCMD
        LDA IO_DSTAT
        BEQ WRITESECT_OK
        LDA #ERR_DISK
        RTS
WRITESECT_OK:
        LDA #0
        RTS

; ---------------------------------------------------------------------------
; Tables and vectors
; ---------------------------------------------------------------------------

; ROWOFF: row * 40, for the screen and the colour map alike.
ROWOFF:
        .word 0, 40, 80, 120, 160, 200, 240, 280
        .word 320, 360, 400, 440, 480, 520, 560, 600
        .word 640, 680, 720, 760, 800, 840, 880, 920
        .word 960

        .include "dos.s"

        .org $FFFA
        .word NMI
        .word RESET
        .word IRQ
