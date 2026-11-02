; SPDX-License-Identifier: MIT
;
; kernal.s -- the Kestrel 16's KERNAL: the tube, the keyboard, the frame, the disk.
;
; This is the part of the machine that is always there. It knows how to put a
; character on the screen, how to read a line, how to move a sector. It knows
; nothing about any language: at the end of its reset it starts the language in
; bank one, and from then on the language owns the machine and the KERNAL is a
; set of services it calls through the jump table below.
;
; The 65816 is used as one: native mode from the first instruction, sixteen-bit
; index registers throughout, and the block-move instruction for the scrolling
; that the Kestrel had to do a byte at a time.

        .include "kestrel16.inc"

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
        jmp DOSLOAD            ; $E02A
        jmp DOSSAVE             ; $E02D
        jmp DOSDELETE           ; $E030
        jmp DOSSTATUS           ; $E033
        jmp DOSFORMAT           ; $E036
        jmp SETINK              ; $E039
        jmp SETPAPER            ; $E03C
        jmp SOUND               ; $E03F
        jmp FRAMES              ; $E042
        jmp MEMTOP              ; $E045
        jmp PEEKBYTE            ; $E048
        jmp POKEBYTE            ; $E04B
        jmp GETKEY              ; $E04E
        jmp NEWLINE             ; $E051

; ---------------------------------------------------------------------------
; Reset, the frame, and BREAK
; ---------------------------------------------------------------------------

        .org KERNAL+$60

RESET:
        SEI
        CLD
        LDX.B #$FF              ; emulation mode: the stack pointer is a byte
        TXS
        CLC
        XCE                     ; native mode: the machine is a 65816
        REP #$30
        LDX.W #$01FF
        TXS
        LDA.W #0
        TCD                     ; direct page at zero
        SEP #$20                ; characters are bytes, addresses are words
        LDA #0
        PHA
        PLB                     ; data bank zero
        REP #$10                ; sixteen-bit index registers from here on
        LDA #0
        STA ZP_COL
        STA ZP_ROW
        STA ZP_INLEN
        STA ZP_INLEN+1
        STA ZP_KBHEAD
        STA ZP_KBTAIL
        STA ZP_KBCOUNT
        STA ZP_IRQCNT
        STA ZP_BLINK
        STA ZP_OFFSET
        STA ZP_OFFSET+1
        STA IO_CURSOR
        STA IO_FRAMEFLAG
        LDA #LIGHTBLUE
        STA IO_BORDER
        LDA #LIGHTGREY
        STA IO_INK
        LDA #BLACK
        STA IO_PAPER
        JSR CLS
        CLI
        JSR BANNER
        JML BASIC

BANNER:
        LDA #<BANNER_TEXT
        STA ZP_PTR
        LDA #>BANNER_TEXT
        STA ZP_PTR+1
        LDA #BANK_KERNAL
        STA ZP_PTR+2
        LDX #BANNER_LENGTH
        JSR PRINT
        JSR NEWLINE
        RTS

BANNER_TEXT:
        .text "Kestrel 16  1992"
BANNER_END:
BANNER_LENGTH = BANNER_END - BANNER_TEXT

; The frame interrupt counts frames, blinks the cursor and empties the keyboard
; controller into the KERNAL's buffer -- which it must do, because the
; controller's interrupt line stays high until its last key is read.
IRQ:
        PHP
        SEP #$20
        PHA
        PHX
        PHY
        LDA IO_FRAMEFLAG
        BEQ IRQ_KEYS
        LDA #0
        STA IO_FRAMEFLAG
        LDA ZP_COL
        STA IO_CURCOL
        LDA ZP_ROW
        STA IO_CURROW
        INC ZP_IRQCNT
        LDA ZP_IRQCNT
        AND #31
        BNE IRQ_KEYS
        LDA IO_CURSOR
        EOR #$80
        STA IO_CURSOR
IRQ_KEYS:
        LDA IO_KBDSTAT
        BPL IRQ_DONE
        LDA IO_KBDDATA
        JSR KBPUSH
        BRA IRQ_KEYS
IRQ_DONE:
        PLY
        PLX
        PLA
        PLP
        RTI

; BREAK is a warm start: clear the line and start the language again.
NMI:
        CLI                     ; interrupts were masked on the way in; the machine
        JSR NEWLINE             ; needs them back to see its keyboard
        JML BASIC

BRK_HANDLER:
        JSR NEWLINE
        JML BASIC

; ---------------------------------------------------------------------------
; The keyboard
; ---------------------------------------------------------------------------

; KBPUSH: A = key. Drops a key when the buffer is full, which is what a machine
; with thirty-two places would do rather than overwrite what is waiting.
KBPUSH:
        PHP
        SEP #$20
        STA ZP_CHAR
        REP #$10
        LDA ZP_KBCOUNT
        CMP #KEYBUF_SIZE
        BCS KBPUSH_DONE
        LDX.W #0
        LDA ZP_KBHEAD
        TAX
        LDA ZP_CHAR
        STA KEYBUF,X
        LDA ZP_KBHEAD
        INC A
        AND #(KEYBUF_SIZE-1)
        STA ZP_KBHEAD
        INC ZP_KBCOUNT
KBPUSH_DONE:
        PLP
        RTS

; GETIN: A = the next key, or zero when none is waiting.
GETIN:
        PHP
        SEP #$20
        REP #$10
        LDA ZP_KBCOUNT
        BEQ GETIN_NONE
        LDX.W #0
        LDA ZP_KBTAIL
        TAX
        LDA KEYBUF,X
        STA ZP_CHAR
        LDA ZP_KBTAIL
        INC A
        AND #(KEYBUF_SIZE-1)
        STA ZP_KBTAIL
        DEC ZP_KBCOUNT
        LDA ZP_CHAR
        BRA GETIN_DONE
GETIN_NONE:
        LDA #0
GETIN_DONE:
        PLP
        RTS

; CHRIN: the next key, waiting for one if it has to.
CHRIN:
        PHP
        SEP #$20
CHRIN_LOOP:
        JSR GETIN
        CMP #0
        BEQ CHRIN_LOOP
        PLP
        RTS

; GETKEY: the same, and it echoes nothing.
GETKEY:
        JMP CHRIN

; ---------------------------------------------------------------------------
; The tube
; ---------------------------------------------------------------------------

; CHROUT: A = character.
CHROUT:
        PHP
        SEP #$20
        REP #$10
        CMP #KEY_RETURN
        BEQ CHROUT_NEWLINE
        CMP #KEY_DELETE
        BEQ CHROUT_BACK
        CMP #32
        BCC CHROUT_DONE
        JSR PUTCHAR
        BRA CHROUT_DONE
CHROUT_NEWLINE:
        JSR NEWLINE
        BRA CHROUT_DONE
CHROUT_BACK:
        JSR BACKSPACE
CHROUT_DONE:
        PLP
        RTS

; PUTCHAR: A = character, drawn at the cursor and the cursor advanced.
PUTCHAR:
        PHP
        SEP #$20
        REP #$10
        PHX
        PHY
        STA ZP_CHAR
        JSR CELLOFFSET
        LDA ZP_CHAR
        STA SCREEN,X
        LDA IO_INK
        ASL A
        ASL A
        ASL A
        ASL A
        ORA IO_PAPER
        STA ATTRS,X
        INC ZP_COL
        LDA ZP_COL
        CMP #COLUMNS
        BCC PUTCHAR_DONE
        JSR NEWLINE
PUTCHAR_DONE:
        PLY
        PLX
        PLP
        RTS

; BACKSPACE: step back one cell and rub it out.
BACKSPACE:
        PHP
        SEP #$20
        REP #$10
        LDA ZP_COL
        BEQ BACKSPACE_DONE
        DEC ZP_COL
        JSR CELLOFFSET
        LDA #32
        STA SCREEN,X
        LDA IO_INK
        ASL A
        ASL A
        ASL A
        ASL A
        ORA IO_PAPER
        STA ATTRS,X
BACKSPACE_DONE:
        PLP
        RTS

; NEWLINE: the carriage returns and the screen scrolls if it has to.
NEWLINE:
        PHP
        SEP #$20
        REP #$10
        STA ZP_CHAR
        LDA #0
        STA ZP_COL
        INC ZP_ROW
        LDA ZP_ROW
        CMP #ROWS
        BCC NEWLINE_ROW_OK
        JSR SCROLL
        LDA #ROWS-1
        STA ZP_ROW
NEWLINE_ROW_OK:
        LDA ZP_CHAR
        PLP
        RTS

; X = row * 80 + column. The tube's position is the pair in ZP_ROW and ZP_COL
; and nothing else, so the two can never drift apart.
CELLOFFSET:
        PHP
        SEP #$20
        REP #$20
        LDA ZP_ROW
        AND.W #$00FF
        STA ZP_MUL1
        ASL A
        ASL A
        ASL A
        ASL A
        STA ZP_MUL2
        LDA ZP_MUL1
        ASL A
        ASL A
        ASL A
        ASL A
        ASL A
        ASL A
        CLC
        ADC ZP_MUL2             ; row * 80
        STA ZP_MUL1
        SEP #$20
        LDA ZP_COL
        REP #$20
        AND.W #$00FF
        CLC
        ADC ZP_MUL1
        TAX
        SEP #$20
        PLP
        RTS

; SCROLL: the whole screen up one row, in two block moves.
SCROLL:
        PHP
        SEP #$20
        REP #$30
        LDA.W #((ROWS-1)*COLUMNS-1)
        LDX.W #(SCREEN+COLUMNS)
        LDY.W #SCREEN
        MVN $00,$00
        LDA.W #((ROWS-1)*COLUMNS-1)
        LDX.W #(ATTRS+COLUMNS)
        LDY.W #ATTRS
        MVN $00,$00
        SEP #$20
        LDA IO_INK
        ASL A
        ASL A
        ASL A
        ASL A
        ORA IO_PAPER
        LDX.W #((ROWS-1)*COLUMNS)
SCROLL_ATTRS:
        STA ATTRS,X
        INX
        CPX #CELLS
        BCC SCROLL_ATTRS
        LDA #32
        LDX.W #((ROWS-1)*COLUMNS)
SCROLL_SPACES:
        STA SCREEN,X
        INX
        CPX #CELLS
        BCC SCROLL_SPACES
        LDA #$80
        STA IO_SCROLLED
        SEP #$30
        PLP
        RTS

; CLS: spaces in the current colours, and the cursor home.
CLS:
        PHP
        SEP #$20
        REP #$10
        LDA #32
        LDX.W #0
CLS_SPACES:
        STA SCREEN,X
        INX
        CPX.W #CELLS
        BCC CLS_SPACES
        LDA IO_INK
        ASL A
        ASL A
        ASL A
        ASL A
        ORA IO_PAPER
        LDX.W #0
CLS_ATTRS:
        STA ATTRS,X
        INX
        CPX.W #CELLS
        BCC CLS_ATTRS
        JSR HOME
        PLP
        RTS

HOME:
        PHP
        SEP #$20
        LDA #0
        STA ZP_COL
        STA ZP_ROW
        STA IO_CURCOL
        STA IO_CURROW
        PLP
        RTS

; LOCATE: X = column, Y = row.
LOCATE:
        PHP
        SEP #$20
        REP #$10
        SEP #$10
        TXA
        STA ZP_COL
        TYA
        STA ZP_ROW
        REP #$10
        PLP
        RTS

; PRINT: ZP_PTR = text, X = length.
PRINT:
        PHP
        SEP #$20
        REP #$10
        STX ZP_TMP
        LDY #0
PRINT_LOOP:
        CPY ZP_TMP
        BCS PRINT_DONE
        LDA [ZP_PTR],Y
        JSR CHROUT
        INY
        BRA PRINT_LOOP
PRINT_DONE:
        PLP
        RTS

CURSORON:
        PHP
        SEP #$20
        STA IO_CURSOR
        PLP
        RTS

SETINK:
        PHP
        SEP #$20
        STA IO_INK
        PLP
        RTS

SETPAPER:
        PHP
        SEP #$20
        STA IO_PAPER
        PLP
        RTS

SOUND:
        PHP
        SEP #$20
        STA IO_SOUND
        PLP
        RTS

; FRAMES: A = the frame counter's low byte, X = its high byte.
FRAMES:
        PHP
        SEP #$20
        LDA IO_FRAMELO
        STA ZP_CHAR
        LDA IO_FRAMEHI
        REP #$10
        TAX
        LDA ZP_CHAR
        PLP
        RTS

; MEMTOP: X = the first byte of free RAM above the program area.
MEMTOP:
        PHP
        REP #$10
        LDX.W #RAMTOP
        PLP
        RTS

; PEEKBYTE: A = bank, X = address. Returns the byte in A.
PEEKBYTE:
        PHP
        PHB
        SEP #$20
        PHA
        PLA
        PLB
        REP #$20
        TXA
        TAX
        SEP #$20
        LDA $0000,X
        PLB
        PLP
        RTS

; POKEBYTE: A = bank, X = address, Y = value.
POKEBYTE:
        PHP
        PHB
        SEP #$20
        PHA
        PLA
        PLB
        REP #$20
        TXA
        TAX
        SEP #$20
        TYA
        STA $0000,X
        PLB
        PLP
        RTS

; ---------------------------------------------------------------------------
; GETLINE: read a line into the buffer at ZP_PTR, return its length in X.
; ---------------------------------------------------------------------------

GETLINE:
        PHP
        SEP #$20
        REP #$10
        LDA #0
        STA ZP_INLEN
        STA ZP_INLEN+1
GETLINE_LOOP:
        JSR CHRIN
        CMP #KEY_RETURN
        BEQ GETLINE_DONE
        CMP #KEY_DELETE
        BEQ GETLINE_BACK
        CMP #32
        BCC GETLINE_LOOP
        PHA                     ; CHROUT uses the accumulator, so keep the key
        JSR CHROUT
        PLA
        LDY ZP_INLEN
        STA [ZP_PTR],Y
        REP #$20
        INC ZP_INLEN
        SEP #$20
        BRA GETLINE_LOOP
GETLINE_BACK:
        REP #$20
        LDA ZP_INLEN
        BEQ GETLINE_BACK_DONE
        DEC ZP_INLEN
        SEP #$20
        JSR BACKSPACE
        BRA GETLINE_LOOP
GETLINE_BACK_DONE:
        SEP #$20
        BRA GETLINE_LOOP
GETLINE_DONE:
        LDY ZP_INLEN
        LDA #0
        STA [ZP_PTR],Y
        REP #$20
        LDX ZP_INLEN
        SEP #$20
        JSR NEWLINE
        PLP
        RTS

; ---------------------------------------------------------------------------
; The disk: one sector at a time, to and from anywhere in the address space.
; ---------------------------------------------------------------------------

; READSECT: X = sector, ZP_PTR = destination. Returns A = 0 or an error.
READSECT:
        LDY #DISK_READ
        BRA SECTMOVE
WRITESECT:
        LDY #DISK_WRITE
SECTMOVE:
        PHP
        SEP #$20
        STX ZP_TMP
        TXA
        STA IO_SECTORLO
        LDA ZP_TMP+1
        STA IO_SECTORHI
        LDA ZP_PTR
        STA IO_ADDRLO
        LDA ZP_PTR+1
        STA IO_ADDRHI
        LDA ZP_PTR+2
        STA IO_ADDRBANK
        LDA #1
        STA IO_COUNTLO
        LDA #0
        STA IO_COUNTHI
        TYA
        STA IO_DISKCMD
        LDA IO_DISKSTAT
        AND #DISK_ERROR
        PLP
        RTS

; ---------------------------------------------------------------------------
; The filesystem services. Kestrel DOS 2.0 is written in this bank beside the
; KERNAL; until it is, a call says so rather than doing something surprising.
; ---------------------------------------------------------------------------

; The volume format, first: sector 0 holds the signature, the name, how many
; sectors there are and the free map. Everything else on the disk is forgotten,
; which is what FORMAT means.
;
;   sector 0, offset 0   "HBDOS20" and a zero
;   offset 8             the volume name, sixteen bytes, space padded
;   offset 24            the sector count, low byte first
;   offset 26            the free map: one bit per sector, a set bit is free
;
; A 512-byte sector and 4096 of them means the map needs 512 bytes on its own, so
; it starts at offset 32 and runs to the end of the header.
DOSFORMAT:
        PHP
        SEP #$20
        REP #$10
        ; The header is built in bank two, which is RAM and nobody else's.
        LDA #2
        STA ZP_PTR+2
        LDA #0
        STA ZP_PTR
        STA ZP_PTR+1
        LDY #0
        LDA #0
FMT_ZERO:
        STA [ZP_PTR],Y
        INY
        CPY #512
        BNE FMT_ZERO

        ; The signature, so a later version can tell its volumes from rubbish.
        ; The zeroing loop left Y at the end of the buffer, so start it again.
        LDY #0
        LDX #0
FMT_SIG:
        LDA FMT_SIGNATURE,X
        STA [ZP_PTR],Y
        INY
        INX
        CPX #7                  ; the signature is seven characters
        BNE FMT_SIG

        ; The name: sixteen spaces until a way to pass one exists. It starts at
        ; offset 8 whatever the signature's length was, so say so.
        LDY #8
        LDX #0
FMT_NAME:
        LDA #32
        STA [ZP_PTR],Y
        INY
        INX
        CPX #16
        BNE FMT_NAME

        ; How many sectors the volume has: 4096, low byte first.
        LDA #$00
        STA [ZP_PTR],Y
        INY
        LDA #$10
        STA [ZP_PTR],Y

        ; Sector 0 is the header itself and the directory has not been written
        ; yet, so a freshly formatted volume is free except for sector 0.
        LDY #32
        LDA #%00000001
        STA [ZP_PTR],Y

        ; Write it out, and say whether the controller agreed.
        LDX #0
        LDY #DISK_WRITE
        JSR WRITESECT
        CMP #0
        BNE FMT_ERR
        ; And an empty directory beside it. The buffer still holds the header, so
        ; it has to be cleared again: sixteen entries of thirty-two zero bytes,
        ; which is what an empty directory is.
        LDA #2
        STA ZP_PTR+2
        STZ ZP_PTR
        STZ ZP_PTR+1
        LDY #0
        LDA #0
FMT_CLEAR2:
        STA [ZP_PTR],Y
        INY
        CPY #512
        BNE FMT_CLEAR2
        LDX #1
        LDY #DISK_WRITE
        JSR WRITESECT
        BEQ FMT_OK
        LDA #1
        PLP
        RTS
FMT_OK:
        LDA #0
        PLP
        RTS
FMT_ERR:
        LDA #1
        PLP
        RTS

; DOSSTATUS: zero when the controller is content.
DOSSTATUS:
        PHP
        SEP #$20
        LDA IO_DISKSTAT
        AND #DISK_ERROR
        PLP
        RTS

; The other services are not written yet, and say so rather than guessing.
DOSCATALOG:
        PHP
        SEP #$20
        REP #$10
        ; The line starts before the read: `[` goes out first, and only then is the
        ; buffer pointer set, because CHROUT moves ZP_PTR as it prints.
        LDA #'['
        JSR CHROUT
        SEP #$20
        REP #$10
        LDA #2
        STA ZP_PTR+2
        STZ ZP_PTR
        STZ ZP_PTR+1
        LDX #0
        LDY #DISK_READ
        ; The sector routines end with PLP, so the flags that come back are the
        ; caller's, not the status: compare before branching on it.
        JSR READSECT
        CMP #0
        BEQ CAT_OK
        ; A read that fails says so, with the controller's code, rather than
        ; printing half a line as though all were well.
        PHA
        LDA #'?'
        JSR CHROUT
        PLA
        CLC
        ADC #'0'
        JSR CHROUT
        JSR NEWLINE
        LDA #1
        PLP
        RTS
CAT_OK:
        LDY #8
        LDX #0
CAT_NAME:
        LDA [ZP_PTR],Y
        JSR CHROUT
        INY
        INX
        CPX #16
        BNE CAT_NAME
        LDA #']'
        JSR CHROUT
        JSR NEWLINE
        LDA #0
        PLP
        RTS
CAT_ERR2:
        LDA #1
        PLP
        RTS

DOSFIND:
DOSLOAD:
DOSSAVE:
DOSDELETE:
        LDA #1
        RTS

FMT_SIGNATURE:
        .text "HBDOS20"


; ---------------------------------------------------------------------------
; The same services for a caller in another bank. A `JSR` stays in the
; program bank, so the language in bank one reaches the KERNAL with `JSL`
; through this table, and each entry returns with `RTL`.
; ---------------------------------------------------------------------------

        .org KERNAL+$1F00

K2_CHROUT:
        JSR CHROUT
        RTL
K2_GETIN:
        JSR GETIN
        RTL
K2_CHRIN:
        JSR CHRIN
        RTL
K2_CLS:
        JSR CLS
        RTL
K2_HOME:
        JSR HOME
        RTL
K2_PRINT:
        JSR PRINT
        RTL
K2_SCROLL:
        JSR SCROLL
        RTL
K2_LOCATE:
        JSR LOCATE
        RTL
K2_GETLINE:
        JSR GETLINE
        RTL
K2_READSECT:
        JSR READSECT
        RTL
K2_WRITESECT:
        JSR WRITESECT
        RTL
K2_CURSORON:
        JSR CURSORON
        RTL
K2_DOSCATALOG:
        JSR DOSCATALOG
        RTL
K2_DOSFIND:
        JSR DOSFIND
        RTL
K2_DOSLOAD:
        JSR DOSLOAD
        RTL
K2_DOSSAVE:
        JSR DOSSAVE
        RTL
K2_DOSDELETE:
        JSR DOSDELETE
        RTL
K2_DOSSTATUS:
        JSR DOSSTATUS
        RTL
K2_DOSFORMAT:
        JSR DOSFORMAT
        RTL
K2_SETINK:
        JSR SETINK
        RTL
K2_SETPAPER:
        JSR SETPAPER
        RTL
K2_SOUND:
        JSR SOUND
        RTL
K2_FRAMES:
        JSR FRAMES
        RTL
K2_MEMTOP:
        JSR MEMTOP
        RTL
K2_PEEKBYTE:
        JSR PEEKBYTE
        RTL
K2_POKEBYTE:
        JSR POKEBYTE
        RTL
K2_GETKEY:
        JSR GETKEY
        RTL
K2_NEWLINE:
        JSR NEWLINE
        RTL

; ---------------------------------------------------------------------------
; The vectors. The native table first, then the three the 6502 mode of the
; processor looks in -- the machine leaves reset through the last of those.
; ---------------------------------------------------------------------------

        .org KERNAL+$1FE4
        .word BRK_HANDLER       ; COP
        .word BRK_HANDLER       ; BRK
        .word BRK_HANDLER       ; ABORT
        .word NMI               ; NMI
        .word BRK_HANDLER       ; unused
        .word IRQ               ; IRQ

        .org KERNAL+$1FFA
        .word NMI
        .word RESET
        .word IRQ
