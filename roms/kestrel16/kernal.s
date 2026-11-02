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
;   offset 26            the free map: one bit per sector, a set bit is *taken*
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

        ; Sector 0 is the header and sector 1 is the directory: both are the
        ; system's, so both are taken before a file can be given anything, and
        ; everything else is free.
        LDY #32
        LDA #%00000011
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

; ---------------------------------------------------------------------------
; Kestrel DOS 2.0
;
; Two sectors of system area, then whatever the free map says is free:
;
;   sector 0        "HBDOS20", the volume's name, its sector count, the free map
;   sector 1        sixteen entries of thirty-two bytes
;   sector 2 up     file data, whole sectors
;
; A file is a run of consecutive sectors, so an entry is a name, where the run
; starts and how many sectors it is: there is nothing chained and nothing to
; follow, and one sector of buffer reads any file there is. The 1982 DOS had to
; chain its sectors because a 1982 file was a stream of bytes; here the files
; are a program store, and its sectors are written and read in order.
;
; The free map is one bit per sector, set when the sector is *taken* -- the same
; way round as the 1982 DOS and its FORMAT, which is what makes the two volumes
; the same format at two sizes. It runs from offset 32 to the end of the header:
; 480 bytes, 3840 bits, so the top 256 sectors of the volume have no bit and are
; never allocated.
;
; Every entry below leaves the accumulator eight bits wide and the index
; registers sixteen, as the KERNAL does, and returns a status in A: zero for
; what was asked for, and one of DOS_MISSING, DOS_IO or DOS_FULL for what was
; not.
; ---------------------------------------------------------------------------

DOS_BANK        = 2             ; the RAM the filesystem works in: nobody else's
DOS_HDRBUF      = $020000       ; sector 0, the volume header
DOS_DIRBUF      = $020200       ; sector 1, the directory
DOS_SECBUF      = $020400       ; one file sector, on its way in or out
DOS_HDR_LO      = DOS_HDRBUF & $FFFF
DOS_DIR_LO      = DOS_DIRBUF & $FFFF
DOS_SEC_LO      = DOS_SECBUF & $FFFF

DOS_ENTRIES     = 16
DOS_ENTRY_SIZE  = 32
DOS_NAME_LEN    = 16
DOS_ENT_FIRST   = 16
DOS_ENT_COUNT   = 18
DOS_NAME_OFF    = 8             ; where the volume's name starts in the header
DOS_MAP_OFFSET  = 32            ; and where the free map does
DOS_MAP_SECTORS = 3840          ; the sectors the map has a bit for
DOS_FIRST_DATA  = 2             ; 0 and 1 belong to the system
DOS_SECTOR_SIZE = 512

DOS_OK          = 0
DOS_MISSING     = 1             ; no such file
DOS_IO          = 2             ; the controller would not
DOS_FULL        = 3             ; no room in the directory or on the volume

; ---------------------------------------------------------------------------
; The buffers, and the one place a sector pointer is set
; ---------------------------------------------------------------------------

; DOS_POINT: X = a buffer's address in bank two, ZP_DOSPTR = that buffer. X is
; left alone, so a caller can keep its own index in it.
DOS_POINT:
        REP #$20
        TXA
        STA ZP_DOSPTR
        SEP #$20
        LDA #DOS_BANK
        STA ZP_DOSPTR+2
        REP #$10
        RTS

; DOS_MOVE: the sector in ZP_DOSSEC goes to or from the buffer in ZP_DOSPTR.
; Y = DISK_READ or DISK_WRITE. A = the controller's status.
;
; The machine's own pointer is set here, from the filesystem's, every time:
; ZP_PTR is what CHROUT and PRINT move, and a read after a catalogue would
; otherwise land somewhere nobody asked for.
DOS_MOVE:
        PHP
        SEP #$20
        REP #$10
        LDA ZP_DOSPTR
        STA ZP_PTR
        LDA ZP_DOSPTR+1
        STA ZP_PTR+1
        LDA ZP_DOSPTR+2
        STA ZP_PTR+2
        LDX ZP_DOSSEC
        JSR SECTMOVE            ; Y still holds the command
        ; SECTMOVE ends with PLP, so the flags here are ours: compare the status
        ; rather than trusting a branch flag from inside it.
        CMP #0
        PLP
        RTS

DOS_RDHDR:
        LDX #DOS_HDR_LO
        JSR DOS_POINT
        REP #$20
        STZ ZP_DOSSEC
        SEP #$20
        LDY #DISK_READ
        JMP DOS_MOVE

DOS_WRHDR:
        LDX #DOS_HDR_LO
        JSR DOS_POINT
        REP #$20
        STZ ZP_DOSSEC
        SEP #$20
        LDY #DISK_WRITE
        JMP DOS_MOVE

DOS_RDDIR:
        LDX #DOS_DIR_LO
        JSR DOS_POINT
        REP #$20
        LDA.W #1
        STA ZP_DOSSEC
        SEP #$20
        LDY #DISK_READ
        JMP DOS_MOVE

DOS_WRDIR:
        LDX #DOS_DIR_LO
        JSR DOS_POINT
        REP #$20
        LDA.W #1
        STA ZP_DOSSEC
        SEP #$20
        LDY #DISK_WRITE
        JMP DOS_MOVE

; DOS_RDSEC / DOS_WRSEC: the sector in ZP_DOSSEC, into or out of DOS_SECBUF.
DOS_RDSEC:
        LDX #DOS_SEC_LO
        JSR DOS_POINT
        LDY #DISK_READ
        JMP DOS_MOVE

DOS_WRSEC:
        LDX #DOS_SEC_LO
        JSR DOS_POINT
        LDY #DISK_WRITE
        JMP DOS_MOVE

; ---------------------------------------------------------------------------
; The free map
; ---------------------------------------------------------------------------

; DOS_MAPADDR: X = a sector, ZP_DOSPTR = its byte in the free map and
; ZP_DOSFLAG = its bit. X comes back as the bit's number, so a caller that wants
; the sector again has to keep it somewhere else.
DOS_MAPADDR:
        REP #$20
        TXA
        LSR A
        LSR A
        LSR A
        CLC
        ADC.W #(DOS_HDR_LO+DOS_MAP_OFFSET)
        STA ZP_DOSPTR
        SEP #$20
        LDA #DOS_BANK
        STA ZP_DOSPTR+2
        REP #$20
        TXA
        AND.W #7
        TAX
        SEP #$20
        LDA.L DOS_MAPBITS,X
        STA ZP_DOSFLAG
        REP #$10
        RTS

DOS_MAPBITS:
        .byte 1, 2, 4, 8, 16, 32, 64, 128

; DOS_MAPTEST: X = a sector. Carry set when the map says it is free -- which is
; a clear bit, because a set bit is a sector with something in it.
DOS_MAPTEST:
        JSR DOS_MAPADDR
        LDA [ZP_DOSPTR]
        AND ZP_DOSFLAG
        REP #$10
        BNE MAPTEST_TAKEN
        SEC
        RTS
MAPTEST_TAKEN:
        CLC
        RTS

; DOS_MAPSET: X = a sector, which the map now says is taken.
DOS_MAPSET:
        JSR DOS_MAPADDR
        LDA [ZP_DOSPTR]
        ORA ZP_DOSFLAG
        STA [ZP_DOSPTR]
        REP #$10
        RTS

; DOS_MAPFREE: X = a sector, which the map now says is free.
DOS_MAPFREE:
        JSR DOS_MAPADDR
        LDA ZP_DOSFLAG
        EOR #$FF
        STA ZP_DOSFLAG          ; now the bit to clear
        LDA [ZP_DOSPTR]
        AND ZP_DOSFLAG
        STA [ZP_DOSPTR]
        REP #$10
        RTS

; DOS_ALLOCRUN: ZP_DOSCOUNT = how many sectors in a row are wanted. The first of
; them comes back in ZP_DOSFIRST, all of them are marked taken, and carry is set
; when the volume has no such run.
;
; A run rather than a chain to keep the reading of a file to one sector at a
; time, and consecutive rather than scattered so that a file is never in more
; pieces than a volume needs.
DOS_ALLOCRUN:
        REP #$20
        LDA.W #DOS_FIRST_DATA
        STA ZP_DOSSCAN
ALLOC_TRY:
        ; The width is set here and not only at the entry: the scan below runs in
        ; eight-bit arithmetic, and the branch back to this label arrives with it.
        ; A `CMP.W` read in eight-bit mode takes two bytes, not three, and leaves
        ; the processor executing the middle of the instruction after it.
        REP #$20
        ; the run has to end where the map still has a bit for it
        LDA ZP_DOSSCAN
        CLC
        ADC ZP_DOSCOUNT
        CMP.W #DOS_MAP_SECTORS
        BCS ALLOC_FULL
        LDA ZP_DOSCOUNT
        STA ZP_DOSLEFT
        LDX ZP_DOSSCAN
        SEP #$20
ALLOC_SCAN:
        JSR DOS_MAPTEST
        BCC ALLOC_NEXT
        INX
        REP #$20
        LDA ZP_DOSLEFT
        DEC A
        STA ZP_DOSLEFT
        BEQ ALLOC_FOUND
        SEP #$20
        BRA ALLOC_SCAN
ALLOC_NEXT:
        REP #$20
        LDA ZP_DOSSCAN
        INC A
        STA ZP_DOSSCAN
        SEP #$20
        BRA ALLOC_TRY
ALLOC_FOUND:
        ; X is one past the run: walk it from the bottom and mark it taken
        TXA
        SEC
        SBC ZP_DOSCOUNT
        STA ZP_DOSFIRST
        STA ZP_DOSSCAN
        LDA ZP_DOSCOUNT
        STA ZP_DOSLEFT
        SEP #$20
ALLOC_MARK:
        REP #$20
        LDA ZP_DOSLEFT
        BEQ ALLOC_DONE
        DEC A
        STA ZP_DOSLEFT
        SEP #$20
        LDX ZP_DOSSCAN
        JSR DOS_MAPSET
        REP #$20
        LDA ZP_DOSSCAN
        INC A
        STA ZP_DOSSCAN
        SEP #$20
        BRA ALLOC_MARK
ALLOC_DONE:
        LDA #DOS_OK
        CLC
        RTS
ALLOC_FULL:
        SEP #$20
        LDA #DOS_FULL
        SEC
        RTS

; DOS_FREERUN: X = a sector, ZP_DOSCOUNT = how many run from it; their bits go
; back to free.
DOS_FREERUN:
        REP #$20
        TXA
        STA ZP_DOSSCAN
        LDA ZP_DOSCOUNT
        STA ZP_DOSLEFT
        SEP #$20
FREERUN_LOOP:
        REP #$20
        LDA ZP_DOSLEFT
        BEQ FREERUN_DONE
        DEC A
        STA ZP_DOSLEFT
        SEP #$20
        LDX ZP_DOSSCAN
        JSR DOS_MAPFREE
        REP #$20
        LDA ZP_DOSSCAN
        INC A
        STA ZP_DOSSCAN
        SEP #$20
        BRA FREERUN_LOOP
FREERUN_DONE:
        SEP #$20
        RTS

; DOS_STEP: ZP_DOSSRC goes on by one, in sixteen bits.
;
; A memory increment is as wide as the accumulator, and every walk here is in
; eight-bit mode: `INC ZP_DOSSRC` there steps the low byte and carries into
; nothing, so a pointer that passes a page boundary starts the page again.
DOS_STEP:
        REP #$20
        LDA ZP_DOSSRC
        INC A
        STA ZP_DOSSRC
        SEP #$20
        RTS

; ---------------------------------------------------------------------------
; The directory
; ---------------------------------------------------------------------------

; DOS_ENTRYADDR: X = an entry's number, ZP_DOSPTR = the entry in DOS_DIRBUF.
DOS_ENTRYADDR:
        REP #$20
        TXA
        ASL A
        ASL A
        ASL A
        ASL A
        ASL A                   ; thirty-two bytes an entry
        CLC
        ADC.W #DOS_DIR_LO
        STA ZP_DOSPTR
        SEP #$20
        LDA #DOS_BANK
        STA ZP_DOSPTR+2
        REP #$10
        RTS

; DOS_FINDSLOT: the first entry with nothing in it, in X; carry set when all
; sixteen are taken.
DOS_FINDSLOT:
        REP #$10
        LDX #0
FINDSLOT_LOOP:
        CPX.W #DOS_ENTRIES
        BCS FINDSLOT_NONE
        JSR DOS_ENTRYADDR
        LDY #0
        LDA [ZP_DOSPTR],Y
        BEQ FINDSLOT_YES
        INX
        BRA FINDSLOT_LOOP
FINDSLOT_NONE:
        SEC
        RTS
FINDSLOT_YES:
        CLC
        RTS

; DOS_FINDINNER: the directory is already in DOS_DIRBUF. Finds the name in
; ZP_DOSNM: X = its entry, ZP_DOSFIRST and ZP_DOSCOUNT as the entry has them,
; and carry clear. Carry set when no entry has that name.
DOS_FINDINNER:
        REP #$10
        LDX #0
FINDINNER_LOOP:
        CPX.W #DOS_ENTRIES
        BCS FINDINNER_NONE
        JSR DOS_ENTRYADDR
        LDY #0
        LDA [ZP_DOSPTR],Y
        BEQ FINDINNER_NEXT
FINDINNER_CMP:
        LDA [ZP_DOSPTR],Y
        STA ZP_DOSFLAG
        LDA [ZP_DOSNM],Y
        CMP ZP_DOSFLAG
        BNE FINDINNER_NEXT
        INY
        CPY.W #DOS_NAME_LEN
        BNE FINDINNER_CMP
        LDY #DOS_ENT_FIRST
        LDA [ZP_DOSPTR],Y
        STA ZP_DOSFIRST
        INY
        LDA [ZP_DOSPTR],Y
        STA ZP_DOSFIRST+1
        LDY #DOS_ENT_COUNT
        LDA [ZP_DOSPTR],Y
        STA ZP_DOSCOUNT
        INY
        LDA [ZP_DOSPTR],Y
        STA ZP_DOSCOUNT+1
        CLC
        RTS
FINDINNER_NEXT:
        INX
        BRA FINDINNER_LOOP
FINDINNER_NONE:
        SEC
        RTS

; DOS_CLEARENTRY: X = an entry's number, all thirty-two bytes set to zero, which
; is what an entry with nothing in it is.
DOS_CLEARENTRY:
        JSR DOS_ENTRYADDR
        LDY #0
        LDA #0
CLEARENTRY_LOOP:
        STA [ZP_DOSPTR],Y
        INY
        CPY.W #DOS_ENTRY_SIZE
        BNE CLEARENTRY_LOOP
        RTS

; ---------------------------------------------------------------------------
; The program store
; ---------------------------------------------------------------------------

; DOS_PROGSIZE: ZP_DOSLEN = the length of the program store in bytes, the zero
; number that ends it included. Carry is set when the walk reached the end of the
; program area without meeting one.
;
; This is prog.inc's walk, in this bank because the filesystem is here and the
; language is not: a record is a two-byte number, the text, and a zero, and the
; zero number ends the store.
DOS_PROGSIZE:
        REP #$20
        LDA.W #PROGSTART
        STA ZP_DOSSRC
        SEP #$20
        STZ ZP_DOSSRC+2         ; the store is in bank zero
        REP #$10
PROGSIZE_LOOP:
        LDY #0
        LDA [ZP_DOSSRC],Y
        BNE PROGSIZE_TEXT
        INY
        LDA [ZP_DOSSRC],Y
        BNE PROGSIZE_TEXT
        ; the end: the terminator is part of the file
        REP #$20
        LDA ZP_DOSSRC
        SEC
        SBC.W #PROGSTART
        CLC
        ADC.W #2
        STA ZP_DOSLEN
        SEP #$20
        CLC
        RTS
PROGSIZE_TEXT:
        ; the text: from two bytes in, until its terminator. Walked with the
        ; pointer rather than an index so that a store with no terminator in it
        ; stops at the end of the program area instead of running off the bank.
        REP #$20
        LDA ZP_DOSSRC
        CLC
        ADC.W #2
        STA ZP_DOSSRC
        SEP #$20
PROGSIZE_TEXT_LOOP:
        LDA [ZP_DOSSRC]
        BEQ PROGSIZE_NEXT
        JSR DOS_STEP
        REP #$20
        LDA ZP_DOSSRC
        CMP.W #PROG_LIMIT
        BCS PROGSIZE_FULL
        SEP #$20
        BRA PROGSIZE_TEXT_LOOP
PROGSIZE_NEXT:
        JSR DOS_STEP            ; past the text's terminator: the next record
        REP #$20
        LDA ZP_DOSSRC
        CMP.W #(PROG_LIMIT-2)   ; the terminator still has to fit
        BCS PROGSIZE_FULL
        SEP #$20
        BRA PROGSIZE_LOOP
PROGSIZE_FULL:
        SEP #$20
        SEC
        RTS

; DOS_SECTORS: ZP_DOSLEN bytes fill how many sectors? The answer goes in
; ZP_DOSCOUNT. Nothing here is ever empty: a program holds at least the two bytes
; of its terminator, so a length of zero is a mistake rather than a small file.
DOS_SECTORS:
        REP #$20
        LDA ZP_DOSLEN
        BEQ DOS_SECTORS_NONE
        CLC
        ADC.W #(DOS_SECTOR_SIZE-1)
        LSR A
        LSR A
        LSR A
        LSR A
        LSR A
        LSR A
        LSR A
        LSR A
        LSR A
        STA ZP_DOSCOUNT
        SEP #$20
        RTS
DOS_SECTORS_NONE:
        STA ZP_DOSCOUNT
        SEP #$20
        RTS

; ---------------------------------------------------------------------------
; Printing, while a pointer has to survive it
; ---------------------------------------------------------------------------

; DOS_SAY: X = the address of a zero-terminated text in this bank, printed with a
; newline after it.
DOS_SAY:
        PHP
        REP #$20
        TXA
        STA ZP_DOSPTR
        LDA.W #0
        STA ZP_DOSCUR
        SEP #$20
        STZ ZP_DOSPTR+2         ; the texts are in the KERNAL's own bank
DOS_SAY_LOOP:
        REP #$10
        LDY ZP_DOSCUR
        LDA [ZP_DOSPTR],Y
        BEQ DOS_SAY_DONE
        JSR CHROUT
        SEP #$20
        REP #$10
        REP #$20
        LDA ZP_DOSCUR
        INC A
        STA ZP_DOSCUR
        SEP #$20
        BRA DOS_SAY_LOOP
DOS_SAY_DONE:
        JSR NEWLINE
        PLP
        RTS

; DOS_PUTC: print the byte at ZP_DOSPTR, ZP_DOSCUR bytes in, and step on. The
; offset lives in memory because CHROUT is allowed to use every register, and a
; line that scrolls the screen takes them all.
DOS_PUTC:
        REP #$10
        LDY ZP_DOSCUR
        LDA [ZP_DOSPTR],Y
        JSR CHROUT
        SEP #$20
        REP #$10
        REP #$20
        LDA ZP_DOSCUR
        INC A
        STA ZP_DOSCUR
        SEP #$20
        RTS

; ---------------------------------------------------------------------------
; CATALOG: the volume's name, and a line for each file there is.
; ---------------------------------------------------------------------------

DOSCATALOG:
        PHP
        SEP #$20
        REP #$10
        JSR DOS_RDHDR
        CMP #0
        BNE CAT_IOERR
        JSR DOS_RDDIR
        CMP #0
        BNE CAT_IOERR

        ; the volume's name, in brackets, all sixteen bytes of it
        LDA #'['
        JSR CHROUT
        LDX #(DOS_HDR_LO+DOS_NAME_OFF)
        JSR DOS_POINT
        REP #$20
        LDA.W #0
        STA ZP_DOSCUR
        SEP #$20
CAT_VOLUME:
        JSR DOS_PUTC
        REP #$20
        LDA ZP_DOSCUR
        CMP.W #DOS_NAME_LEN
        SEP #$20
        BCC CAT_VOLUME
        LDA #']'
        JSR CHROUT
        JSR NEWLINE

        ; and a line for each entry that has a name in it
        LDA #0
        STA ZP_DOSFLAG          ; nothing listed yet
        REP #$20
        STZ ZP_DOSIDX
        SEP #$20
CAT_LOOP:
        REP #$20
        LDA ZP_DOSIDX
        CMP.W #DOS_ENTRIES
        SEP #$20
        BCS CAT_END
        LDX ZP_DOSIDX
        JSR DOS_ENTRYADDR
        LDY #0
        LDA [ZP_DOSPTR],Y
        BEQ CAT_NEXT
        LDA #1
        STA ZP_DOSFLAG
        REP #$20
        LDA.W #0
        STA ZP_DOSCUR
        SEP #$20
CAT_NAME:
        REP #$10
        LDY ZP_DOSCUR
        LDA [ZP_DOSPTR],Y
        CMP #32                 ; the name stops at its first space
        BEQ CAT_NAME_END
        JSR CHROUT
        SEP #$20
        REP #$10
        REP #$20
        LDA ZP_DOSCUR
        INC A
        STA ZP_DOSCUR
        CMP.W #DOS_NAME_LEN
        SEP #$20
        BCC CAT_NAME
CAT_NAME_END:
        JSR NEWLINE
CAT_NEXT:
        REP #$20
        LDA ZP_DOSIDX
        INC A
        STA ZP_DOSIDX
        SEP #$20
        BRA CAT_LOOP
CAT_END:
        LDA ZP_DOSFLAG
        BNE CAT_OK
        LDX #DOS_TXT_EMPTY
        JSR DOS_SAY
CAT_OK:
        LDA #DOS_OK
        PLP
        RTS
CAT_IOERR:
        LDX #DOS_TXT_DISK
        JSR DOS_SAY
        LDA #DOS_IO
        PLP
        RTS

; ---------------------------------------------------------------------------
; FIND: where a name is.
; ---------------------------------------------------------------------------

; DOSFIND: A = a sixteen-bit pointer to a sixteen-byte space-padded name in bank
; zero. X = the entry it is in, ZP_DOSFIRST = that entry's first sector, and
; carry set when no entry has that name.
DOSFIND:
        PHP
        REP #$20
        STA ZP_DOSNM
        SEP #$20
        STZ ZP_DOSNM+2          ; the name is in bank zero
        REP #$10
        JSR DOS_RDDIR
        CMP #0
        BNE DOSFIND_IOERR
        JSR DOS_FINDINNER
        BCS DOSFIND_NONE
        TXA
        PLP
        CLC
        RTS
DOSFIND_NONE:
        LDA #DOS_MISSING
        PLP
        SEC
        RTS
DOSFIND_IOERR:
        LDA #DOS_IO
        PLP
        SEC
        RTS

; ---------------------------------------------------------------------------
; SAVE: the program store becomes a file.
; ---------------------------------------------------------------------------

; DOSSAVE: A = the name, as DOSFIND takes it. Whatever program is in the store
; is written as that file; a file of the same name is replaced, not added to.
DOSSAVE:
        PHP
        REP #$20
        STA ZP_DOSNM
        SEP #$20
        STZ ZP_DOSNM+2
        REP #$10
        JSR DOS_RDHDR
        CMP #0
        BNE SAVE_IOERR
        JSR DOS_RDDIR
        CMP #0
        BNE SAVE_IOERR

        ; A file of the same name goes first: its sectors back to the map and its
        ; entry cleared, before anything is allocated for the new one.
        JSR DOS_FINDINNER
        BCS SAVE_SLOT
        STX ZP_DOSIDX
        LDX ZP_DOSFIRST
        JSR DOS_FREERUN
        LDX ZP_DOSIDX
        JSR DOS_CLEARENTRY

SAVE_SLOT:
        JSR DOS_PROGSIZE
        BCS SAVE_TOOBIG
        JSR DOS_SECTORS
        REP #$20
        LDA ZP_DOSCOUNT
        BNE SAVE_HAVE_SIZE
        SEP #$20
        BRA SAVE_NO_ROOM        ; an empty store is a mistake, not a small file
SAVE_HAVE_SIZE:
        SEP #$20
        JSR DOS_FINDSLOT
        BCS SAVE_NO_SLOT
        STX ZP_DOSIDX
        JSR DOS_ALLOCRUN
        BCS SAVE_NO_ROOM

        ; the sectors, one at a time, from the store into the buffer
        REP #$20
        LDA ZP_DOSLEN
        STA ZP_DOSLEFT
        LDA ZP_DOSFIRST
        STA ZP_DOSSEC
        LDA.W #PROGSTART
        STA ZP_DOSSRC
        SEP #$20
        STZ ZP_DOSSRC+2
SAVE_SECTOR:
        ; The buffer is cleared before every sector, and not once at the start: a
        ; file whose last sector is short would otherwise carry the bytes of the
        ; sector written before it into the file.
        LDX #DOS_SEC_LO
        JSR DOS_POINT
        LDY #0
        LDA #0
SAVE_CLEAR:
        STA [ZP_DOSPTR],Y
        INY
        CPY.W #DOS_SECTOR_SIZE
        BNE SAVE_CLEAR

        ; what is left, or a whole sector if there is more than that
        REP #$20
        LDA ZP_DOSLEFT
        CMP.W #DOS_SECTOR_SIZE
        BCC SAVE_SHORT
        LDA.W #DOS_SECTOR_SIZE
SAVE_SHORT:
        STA ZP_DOSSIZE
        SEP #$20
        LDY #0
SAVE_COPY:
        CPY ZP_DOSSIZE
        BCS SAVE_WRITE
        LDA [ZP_DOSSRC]
        STA [ZP_DOSPTR],Y
        JSR DOS_STEP
        INY
        BRA SAVE_COPY
SAVE_WRITE:
        JSR DOS_WRSEC
        CMP #0
        BNE SAVE_IOERR
        REP #$20
        LDA ZP_DOSLEFT
        SEC
        SBC ZP_DOSSIZE
        STA ZP_DOSLEFT
        LDA ZP_DOSSEC
        INC A
        STA ZP_DOSSEC
        LDA ZP_DOSLEFT
        BEQ SAVE_DATA_DONE
        SEP #$20
        BRA SAVE_SECTOR

SAVE_DATA_DONE:
        ; and the entry: the name, where the data starts, how much there is. The
        ; reserved bytes are already zero -- the slot was empty or cleared.
        SEP #$20
        LDX ZP_DOSIDX
        JSR DOS_ENTRYADDR
        LDY #0
SAVE_NAME:
        LDA [ZP_DOSNM],Y
        STA [ZP_DOSPTR],Y
        INY
        CPY.W #DOS_NAME_LEN
        BNE SAVE_NAME
        LDY #DOS_ENT_FIRST
        LDA ZP_DOSFIRST
        STA [ZP_DOSPTR],Y
        INY
        LDA ZP_DOSFIRST+1
        STA [ZP_DOSPTR],Y
        LDY #DOS_ENT_COUNT
        LDA ZP_DOSCOUNT
        STA [ZP_DOSPTR],Y
        INY
        LDA ZP_DOSCOUNT+1
        STA [ZP_DOSPTR],Y

        ; the directory first, then the header: a map that has given away sectors
        ; no entry mentions is a leak, but a file with no sectors in the map is a
        ; file somebody will overwrite.
        JSR DOS_WRDIR
        CMP #0
        BNE SAVE_IOERR
        JSR DOS_WRHDR
        CMP #0
        BNE SAVE_IOERR
        LDA #DOS_OK
        PLP
        CLC
        RTS
SAVE_TOOBIG:
SAVE_NO_SLOT:
SAVE_NO_ROOM:
        LDX #DOS_TXT_FULL
        JSR DOS_SAY
        LDA #DOS_FULL
        PLP
        SEC
        RTS
SAVE_IOERR:
        LDX #DOS_TXT_DISK
        JSR DOS_SAY
        LDA #DOS_IO
        PLP
        SEC
        RTS

; ---------------------------------------------------------------------------
; LOAD: a file becomes the program store.
; ---------------------------------------------------------------------------

; DOSLOAD: A = the name, as DOSFIND takes it. The file's sectors are read into
; the store, and ZP_PROGTOP is left at the zero number that ends it, so the
; language can RUN what came back.
DOSLOAD:
        PHP
        REP #$20
        STA ZP_DOSNM
        SEP #$20
        STZ ZP_DOSNM+2
        REP #$10
        JSR DOS_RDDIR
        CMP #0
        BNE LOAD_IOERR
        JSR DOS_FINDINNER
        BCS LOAD_MISSING

        ; An empty store first, with the language's own end in it, so a file that
        ; turns out to be nothing leaves an empty program rather than half of two.
        REP #$20
        LDA.W #0
        STA.L PROGSTART
        LDA.W #PROGSTART
        STA.L PROG_TOP
        STA ZP_DOSSRC           ; where the bytes are going, straight after
        SEP #$20
        STZ ZP_DOSSRC+2
        REP #$20
        LDA ZP_DOSFIRST
        STA ZP_DOSSEC
        LDA ZP_DOSCOUNT
        STA ZP_DOSLEFT
        SEP #$20

LOAD_SECTOR:
        REP #$20
        LDA ZP_DOSLEFT
        BEQ LOAD_DONE
        DEC A
        STA ZP_DOSLEFT
        SEP #$20
        JSR DOS_RDSEC
        CMP #0
        BNE LOAD_IOERR

        ; how much of the store is left to fill: a program may not pass
        ; PROG_LIMIT, and a file that would is a file from somewhere else
        REP #$20
        LDA.W #PROG_LIMIT
        SEC
        SBC ZP_DOSSRC
        CMP.W #DOS_SECTOR_SIZE
        BCC LOAD_PART
        LDA.W #DOS_SECTOR_SIZE
LOAD_PART:
        STA ZP_DOSSIZE
        SEP #$20
        LDX #DOS_SEC_LO
        JSR DOS_POINT
        LDY #0
LOAD_COPY:
        CPY ZP_DOSSIZE
        BCS LOAD_NEXT
        LDA [ZP_DOSPTR],Y
        STA [ZP_DOSSRC]
        JSR DOS_STEP
        INY
        BRA LOAD_COPY
LOAD_NEXT:
        REP #$20
        LDA ZP_DOSSEC
        INC A
        STA ZP_DOSSEC
        SEP #$20
        BRA LOAD_SECTOR

LOAD_DONE:
        ; where the program ends is where its zero number is, which is the same
        ; walk SAVE took. It is not the size of the file: the last sector is
        ; padded, and a program that ended at the padding would grow a sector
        ; every time it was loaded and saved.
        ;
        ; The store is PROGSTART up to the terminator, and PROGTOP is the address
        ; of the terminator itself -- the bytes of the file, less the two the
        ; number takes -- which is the same end NEW leaves for an empty program.
        JSR DOS_PROGSIZE
        BCS LOAD_BAD
        REP #$20
        LDA.W #PROGSTART
        CLC
        ADC ZP_DOSLEN
        SEC
        SBC.W #2
        STA.L PROG_TOP
        SEP #$20
        LDA #DOS_OK
        PLP
        CLC
        RTS
LOAD_BAD:
        REP #$20
        LDA.W #0
        STA.L PROGSTART
        LDA.W #PROGSTART
        STA.L PROG_TOP
        SEP #$20
        LDX #DOS_TXT_BADFILE
        JSR DOS_SAY
        LDA #DOS_IO
        PLP
        SEC
        RTS
LOAD_MISSING:
        LDX #DOS_TXT_NOFILE
        JSR DOS_SAY
        LDA #DOS_MISSING
        PLP
        SEC
        RTS
LOAD_IOERR:
        LDX #DOS_TXT_DISK
        JSR DOS_SAY
        LDA #DOS_IO
        PLP
        SEC
        RTS

; ---------------------------------------------------------------------------
; DELETE: a file's sectors go back to the map and its entry is emptied.
; ---------------------------------------------------------------------------

DOSDELETE:
        PHP
        REP #$20
        STA ZP_DOSNM
        SEP #$20
        STZ ZP_DOSNM+2
        REP #$10
        JSR DOS_RDHDR
        CMP #0
        BNE DELETE_IOERR
        JSR DOS_RDDIR
        CMP #0
        BNE DELETE_IOERR
        JSR DOS_FINDINNER
        BCS DELETE_MISSING
        STX ZP_DOSIDX
        LDX ZP_DOSFIRST
        JSR DOS_FREERUN          ; which is why the entry's number is kept
        LDX ZP_DOSIDX
        JSR DOS_CLEARENTRY
        JSR DOS_WRDIR
        CMP #0
        BNE DELETE_IOERR
        JSR DOS_WRHDR
        CMP #0
        BNE DELETE_IOERR
        LDA #DOS_OK
        PLP
        CLC
        RTS
DELETE_MISSING:
        LDX #DOS_TXT_NOFILE
        JSR DOS_SAY
        LDA #DOS_MISSING
        PLP
        SEC
        RTS
DELETE_IOERR:
        LDX #DOS_TXT_DISK
        JSR DOS_SAY
        LDA #DOS_IO
        PLP
        SEC
        RTS

DOS_TXT_EMPTY:
        .text "EMPTY"
        .byte 0
DOS_TXT_DISK:
        .text "?DISK"
        .byte 0
DOS_TXT_FULL:
        .text "?DISK FULL"
        .byte 0
DOS_TXT_NOFILE:
        .text "?NOT FOUND"
        .byte 0
DOS_TXT_BADFILE:
        .text "?BAD FILE"
        .byte 0

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
