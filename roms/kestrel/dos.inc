; SPDX-License-Identifier: MIT
;
; dos.inc -- Kestrel DOS 1.0, the part of the KERNAL that turns 512 raw sectors into a
; disk with names on it.
;
; The hardware knows two things: read a sector, write a sector. Everything above
; that is here, in ROM, where a 1982 machine would have put it:
;
;   sector 0      the volume header: signature, name, sector count, and a
;                 64-byte free map with one bit per sector
;   sectors 1-2   the directory: 32 entries of 16 bytes
;   sector 3 up   file data, 252 bytes per sector, each sector pointing at the
;                 next one
;
; An entry is: 0-7 name (space padded, first byte 0 = empty), 8-9 first sector,
; 10-11 length in bytes, 12 type ('P'), 13-15 zero.
;
; A data sector is: 0-251 data, 252-253 next sector (0 = last), 254 bytes used,
; 255 zero.

; ---------------------------------------------------------------------------
; Status
; ---------------------------------------------------------------------------

DOSSTATUS:
        LDA ZP_DSKSTAT
        RTS

; ---------------------------------------------------------------------------
; Volume and directory
; ---------------------------------------------------------------------------

; READVOL: the volume header into VOLBUF.
READVOL:
        LDA #<VOLBUF
        STA ZP_PTR
        LDA #>VOLBUF
        STA ZP_PTR+1
        LDA #DOS_VOLUME
        LDX #0
        JMP READSECT

WRITEVOL:
        LDA #<VOLBUF
        STA ZP_PTR
        LDA #>VOLBUF
        STA ZP_PTR+1
        LDA #DOS_VOLUME
        LDX #0
        JMP WRITESECT

; READDIR: both directory sectors into DIRBUF.
READDIR:
        LDA #<DIRBUF
        STA ZP_PTR
        LDA #>DIRBUF
        STA ZP_PTR+1
        LDA #DOS_DIR_FIRST
        LDX #0
        JSR READSECT
        BNE READDIR_DONE
        LDA #<(DIRBUF+256)
        STA ZP_PTR
        LDA #>(DIRBUF+256)
        STA ZP_PTR+1
        LDA #DOS_DIR_FIRST+1
        LDX #0
        JMP READSECT
READDIR_DONE:
        RTS

WRITEDIR:
        LDA #<DIRBUF
        STA ZP_PTR
        LDA #>DIRBUF
        STA ZP_PTR+1
        LDA #DOS_DIR_FIRST
        LDX #0
        JSR WRITESECT
        BNE WRITEDIR_DONE
        LDA #<(DIRBUF+256)
        STA ZP_PTR
        LDA #>(DIRBUF+256)
        STA ZP_PTR+1
        LDA #DOS_DIR_FIRST+1
        LDX #0
        JMP WRITESECT
WRITEDIR_DONE:
        RTS

; ENTRYPTR: A = entry index 0..31 -> ZP_PTR2 = the entry in DIRBUF.
ENTRYPTR:
        STA ZP_TMP
        ASL A
        ASL A
        ASL A
        ASL A
        STA ZP_PTR2
        LDA ZP_TMP
        LSR A
        LSR A
        LSR A
        LSR A
        CLC
        ADC #>DIRBUF
        STA ZP_PTR2+1
        RTS

; ---------------------------------------------------------------------------
; Finding a name
; ---------------------------------------------------------------------------

; FINDINNER: the directory is already loaded. -> A = index or $FF, and
; ZP_PTR2 points at the entry.
FINDINNER:
        LDA #0
        STA ZP_TMP
FINDINNER_LOOP:
        LDA ZP_TMP
        CMP #DOS_MAX_FILES
        BCS FINDINNER_NONE
        JSR ENTRYPTR
        LDY #0
        LDA (ZP_PTR2),Y
        BEQ FINDINNER_NEXT
        LDY #0
FINDINNER_CMP:
        LDA (ZP_PTR2),Y
        CMP DOSNAME,Y
        BNE FINDINNER_NEXT
        INY
        CPY #DOS_NAME_LEN
        BNE FINDINNER_CMP
        LDA ZP_TMP
        RTS
FINDINNER_NEXT:
        INC ZP_TMP
        JMP FINDINNER_LOOP
FINDINNER_NONE:
        LDA #$FF
        RTS

DOSFIND:
        JSR READDIR
        BNE DOSFIND_ERR
        JSR FINDINNER
        RTS
DOSFIND_ERR:
        LDA #$FF
        RTS

; ---------------------------------------------------------------------------
; The free map
; ---------------------------------------------------------------------------

; ALLOC: the lowest free sector, marked used. Carry clear and ZP_PTR2 = sector,
; or carry set when the volume is full.
ALLOC:
        LDA #DOS_FIRST_DATA
        STA ZP_PTR2
        LDA #0
        STA ZP_PTR2+1
ALLOC_LOOP:
        LDA ZP_PTR2+1
        CMP #2
        BCC ALLOC_TRY
        BNE ALLOC_FULL
        LDA ZP_PTR2
        CMP #0
        BCS ALLOC_FULL
ALLOC_TRY:
        JSR MAPBYTE
        AND MAPBIT,Y
        BNE ALLOC_NEXT
        LDA MAPBIT,Y
        ORA VOLBUF+DOS_FREE_MAP,X
        STA VOLBUF+DOS_FREE_MAP,X
        CLC
        RTS
ALLOC_NEXT:
        INC ZP_PTR2
        BNE ALLOC_LOOP
        INC ZP_PTR2+1
        JMP ALLOC_LOOP
ALLOC_FULL:
        SEC
        RTS

; FREE: the sector in ZP_PTR2 gives its bit back.
FREE:
        JSR MAPBYTE
        LDA MAPBIT,Y
        EOR #$FF
        AND VOLBUF+DOS_FREE_MAP,X
        STA VOLBUF+DOS_FREE_MAP,X
        RTS

; MAPBYTE: X = the free-map byte for the sector in ZP_PTR2, Y = the bit.
MAPBYTE:
        LDA ZP_PTR2+1
        ASL A
        ASL A
        ASL A
        ASL A
        ASL A
        STA ZP_TMP
        LDA ZP_PTR2
        LSR A
        LSR A
        LSR A
        CLC
        ADC ZP_TMP
        TAX
        LDA ZP_PTR2
        AND #7
        TAY
        RTS

; ---------------------------------------------------------------------------
; Cataloguing
; ---------------------------------------------------------------------------

DOSCATALOG:
        JSR READVOL
        BNE DOSCATALOG_ERR
        JSR READDIR
        BNE DOSCATALOG_ERR
        LDA #0
        STA ZP_ENTRY            ; printing a line uses every other scratch byte
DOSCATALOG_LOOP:
        LDA ZP_ENTRY
        CMP #DOS_MAX_FILES
        BCS DOSCATALOG_NONE
        JSR ENTRYPTR
        LDY #0
        LDA (ZP_PTR2),Y
        BEQ DOSCATALOG_NEXT
        LDY #0
DOSCATALOG_NAME:
        LDA (ZP_PTR2),Y
        JSR CHROUT
        INY
        CPY #DOS_NAME_LEN
        BNE DOSCATALOG_NAME
        LDA #32
        JSR CHROUT
        LDY #10
        LDA (ZP_PTR2),Y
        STA ZP_PTR
        LDY #11
        LDA (ZP_PTR2),Y
        STA ZP_PTR+1
        JSR PRDEC16
        JSR NEWLINE
DOSCATALOG_NEXT:
        INC ZP_ENTRY
        JMP DOSCATALOG_LOOP
DOSCATALOG_NONE:
        LDA #0
        STA ZP_DSKSTAT
        RTS
DOSCATALOG_ERR:
        LDA #ERR_DISK
        STA ZP_DSKSTAT
        RTS

; ---------------------------------------------------------------------------
; Loading
; ---------------------------------------------------------------------------

; DOSLOAD: the file DOSNAME is copied to (ZP_DSKPTR). On return ZP_DSKPTR and
; ZP_DSKEND hold the end of what was loaded, and A is the status.
DOSLOAD:
        JSR READVOL
        BNE DOSLOAD_ERR
        JSR READDIR
        BNE DOSLOAD_ERR
        JSR FINDINNER
        CMP #$FF
        BEQ DOSLOAD_MISSING
        JSR ENTRYPTR
        LDY #8
        LDA (ZP_PTR2),Y
        STA ZP_TMP            ; current sector
        INY
        LDA (ZP_PTR2),Y
        STA ZP_TMP2
DOSLOAD_SECTOR:
        LDA #<CHUNKBUF
        STA ZP_PTR
        LDA #>CHUNKBUF
        STA ZP_PTR+1
        LDA ZP_TMP
        LDX ZP_TMP2
        JSR READSECT
        BNE DOSLOAD_ERR
        LDA CHUNKBUF+254
        STA ZP_TMP3
        JSR COPYOUT
        LDA CHUNKBUF+252
        STA ZP_TMP
        LDA CHUNKBUF+253
        STA ZP_TMP2
        LDA ZP_TMP
        ORA ZP_TMP2
        BNE DOSLOAD_SECTOR
        LDA #0
        STA ZP_DSKSTAT
        RTS
DOSLOAD_MISSING:
        LDA #ERR_NOT_FOUND
        STA ZP_DSKSTAT
        RTS
DOSLOAD_ERR:
        LDA #ERR_DISK
        STA ZP_DSKSTAT
        RTS

; COPYOUT: ZP_TMP3 bytes from CHUNKBUF to (ZP_DSKPTR), which advances.
COPYOUT:
        LDA #0
        STA ZP_TMP4
COPYOUT_LOOP:
        LDY ZP_TMP4
        CPY ZP_TMP3
        BEQ COPYOUT_DONE
        LDA CHUNKBUF,Y
        LDY #0
        STA (ZP_DSKPTR),Y
        INC ZP_DSKPTR
        BNE COPYOUT_SKIP
        INC ZP_DSKPTR+1
COPYOUT_SKIP:
        INC ZP_TMP4
        JMP COPYOUT_LOOP
COPYOUT_DONE:
        RTS

; ---------------------------------------------------------------------------
; Saving
; ---------------------------------------------------------------------------

; DOSSAVE: ZP_DSKPTR..ZP_DSKEND is written as DOSNAME. A = status.
DOSSAVE:
        JSR READVOL
        BNE DOSSAVE_ERR
        JSR READDIR
        BNE DOSSAVE_ERR

        ; A file of the same name is replaced, not appended to.
        JSR FINDINNER
        CMP #$FF
        BEQ DOSSAVE_SLOT
        PHA                     ; the entry index, across the walk of its sectors
        JSR ENTRYPTR
        JSR FREECHAIN           ; which leaves ZP_PTR2 on the last sector it freed
        PLA
        JSR ENTRYPTR            ; so the entry is found again before it is cleared
        JSR CLEARENTRY

DOSSAVE_SLOT:
        JSR FINDSLOT
        CMP #$FF
        BEQ DOSSAVE_FULL
        STA ZP_TMP4           ; the entry this save goes in
        JSR ENTRYPTR

        ; Remember where the entry is: ZP_PTR2 will be reused by ALLOC.
        LDA ZP_PTR2
        STA SAVEENTRY
        LDA ZP_PTR2+1
        STA SAVEENTRY+1

        ; The length, computed before the pointers start moving.
        LDA ZP_DSKEND
        SEC
        SBC ZP_DSKPTR
        STA SAVELEN
        LDA ZP_DSKEND+1
        SBC ZP_DSKPTR+1
        STA SAVELEN+1
        LDA SAVELEN
        STA SAVETOT
        LDA SAVELEN+1
        STA SAVETOT+1

        LDA #0
        STA SAVEFIRST
        STA SAVEFIRST+1
        STA SAVEPREV
        STA SAVEPREV+1

DOSSAVE_CHUNK:
        ; A full chunk, or whatever is left.
        LDA #DOS_CHUNK
        STA ZP_TMP3
        LDA SAVELEN+1
        BNE DOSSAVE_FULLCHUNK
        LDA SAVELEN
        CMP #DOS_CHUNK
        BCS DOSSAVE_FULLCHUNK
        STA ZP_TMP3
DOSSAVE_FULLCHUNK:
        JSR ALLOC
        BCS DOSSAVE_FULL
        ; ZP_PTR2 = the sector we just took
        LDA ZP_PTR2
        STA ZP_TMP
        LDA ZP_PTR2+1
        STA ZP_TMP2

        JSR COPYIN

        ; Fill the sector: data, no next yet, and the count.
        LDX #0
DOSSAVE_ZERO:
        LDA #0
        STA CHUNKBUF+252,X
        INX
        CPX #4
        BNE DOSSAVE_ZERO
        LDA ZP_TMP3
        STA CHUNKBUF+254

        ; If there is a previous sector, point it at this one.
        LDA SAVEPREV
        ORA SAVEPREV+1
        BEQ DOSSAVE_WRITE
        JSR PATCHPREV

DOSSAVE_WRITE:
        LDA #<CHUNKBUF
        STA ZP_PTR
        LDA #>CHUNKBUF
        STA ZP_PTR+1
        LDA ZP_TMP
        LDX ZP_TMP2
        JSR WRITESECT
        BNE DOSSAVE_ERR

        ; The first sector is the one the directory remembers.
        LDA SAVEFIRST
        ORA SAVEFIRST+1
        BNE DOSSAVE_NOTFIRST
        LDA ZP_TMP
        STA SAVEFIRST
        LDA ZP_TMP2
        STA SAVEFIRST+1
DOSSAVE_NOTFIRST:
        LDA ZP_TMP
        STA SAVEPREV
        LDA ZP_TMP2
        STA SAVEPREV+1

        ; Finished when nothing is left.
        LDA SAVELEN
        ORA SAVELEN+1
        BEQ DOSSAVE_ENTRY
        JMP DOSSAVE_CHUNK

DOSSAVE_ENTRY:
        LDA SAVEENTRY
        STA ZP_PTR2
        LDA SAVEENTRY+1
        STA ZP_PTR2+1
        LDY #0
DOSSAVE_NAME:
        LDA DOSNAME,Y
        STA (ZP_PTR2),Y
        INY
        CPY #DOS_NAME_LEN
        BNE DOSSAVE_NAME
        LDY #8
        LDA SAVEFIRST
        STA (ZP_PTR2),Y
        INY
        LDA SAVEFIRST+1
        STA (ZP_PTR2),Y
        LDY #10
        LDA SAVETOT            ; the length as it was before the copy moved on
        STA (ZP_PTR2),Y
        INY
        LDA SAVETOT+1
        STA (ZP_PTR2),Y
        LDY #12
        LDA #'P'
        STA (ZP_PTR2),Y
        JSR WRITEDIR
        BNE DOSSAVE_ERR
        JSR WRITEVOL
        BNE DOSSAVE_ERR
        LDA #0
        STA ZP_DSKSTAT
        RTS
DOSSAVE_FULL:
        LDA #ERR_NO_ROOM
        STA ZP_DSKSTAT
        RTS
DOSSAVE_MISSING:
        LDA #ERR_NOT_FOUND
        STA ZP_DSKSTAT
        RTS
DOSSAVE_ERR:
        LDA #ERR_DISK
        STA ZP_DSKSTAT
        RTS

; COPYIN: ZP_TMP3 bytes from (ZP_DSKPTR) into CHUNKBUF, advancing both.
COPYIN:
        LDA #0
        STA ZP_TMP4
COPYIN_LOOP:
        LDY ZP_TMP4
        CPY ZP_TMP3
        BEQ COPYIN_DONE
        LDY #0
        LDA (ZP_DSKPTR),Y
        LDY ZP_TMP4
        STA CHUNKBUF,Y
        INC ZP_DSKPTR
        BNE COPYIN_SKIP
        INC ZP_DSKPTR+1
COPYIN_SKIP:
        INC ZP_TMP4
        JMP COPYIN_LOOP
COPYIN_DONE:
        ; What is left to write
        LDA SAVELEN
        SEC
        SBC ZP_TMP3
        STA SAVELEN
        LDA SAVELEN+1
        SBC #0
        STA SAVELEN+1
        RTS

; PATCHPREV: the sector in SAVEPREV learns that ZP_TMP/ZP_TMP2 follows it.
PATCHPREV:
        LDA #<CHUNKBUF
        STA ZP_PTR
        LDA #>CHUNKBUF
        STA ZP_PTR+1
        LDA SAVEPREV
        LDX SAVEPREV+1
        JSR READSECT
        LDA ZP_TMP
        STA CHUNKBUF+252
        LDA ZP_TMP2
        STA CHUNKBUF+253
        LDA #<CHUNKBUF
        STA ZP_PTR
        LDA #>CHUNKBUF
        STA ZP_PTR+1
        LDA SAVEPREV
        LDX SAVEPREV+1
        JMP WRITESECT

; ---------------------------------------------------------------------------
; Deleting
; ---------------------------------------------------------------------------

DOSDELETE:
        JSR READVOL
        BNE DOSDELETE_ERR
        JSR READDIR
        BNE DOSDELETE_ERR
        JSR FINDINNER
        CMP #$FF
        BEQ DOSDELETE_MISSING
        JSR ENTRYPTR
        JSR FREECHAIN
        JSR CLEARENTRY
        JSR WRITEDIR
        BNE DOSDELETE_ERR
        JSR WRITEVOL
        BNE DOSDELETE_ERR
        LDA #0
        STA ZP_DSKSTAT
        RTS
DOSDELETE_MISSING:
        LDA #ERR_NOT_FOUND
        STA ZP_DSKSTAT
        RTS
DOSDELETE_ERR:
        LDA #ERR_DISK
        STA ZP_DSKSTAT
        RTS

; FREECHAIN: the entry at ZP_PTR2 names a chain; every sector in it goes back
; on the free map.
FREECHAIN:
        LDY #8
        LDA (ZP_PTR2),Y
        STA ZP_TMP
        INY
        LDA (ZP_PTR2),Y
        STA ZP_TMP2
FREECHAIN_LOOP:
        LDA ZP_TMP
        ORA ZP_TMP2
        BEQ FREECHAIN_DONE
        ; Read the sector to learn its successor before freeing it.
        LDA #<CHUNKBUF
        STA ZP_PTR
        LDA #>CHUNKBUF
        STA ZP_PTR+1
        LDA ZP_TMP
        LDX ZP_TMP2
        JSR READSECT
        BNE FREECHAIN_DONE
        LDA CHUNKBUF+252
        PHA
        LDA CHUNKBUF+253
        PHA
        LDA ZP_TMP
        STA ZP_PTR2
        LDA ZP_TMP2
        STA ZP_PTR2+1
        JSR FREE
        PLA
        STA ZP_TMP2
        PLA
        STA ZP_TMP
        JMP FREECHAIN_LOOP
FREECHAIN_DONE:
        RTS

; CLEARENTRY: the entry at ZP_PTR2 becomes an empty slot.
CLEARENTRY:
        LDY #0
        LDA #0
CLEARENTRY_LOOP:
        STA (ZP_PTR2),Y
        INY
        CPY #DOS_ENTRY_SIZE
        BNE CLEARENTRY_LOOP
        RTS

; FINDSLOT: -> A = the first empty entry, or $FF.
FINDSLOT:
        LDA #0
        STA ZP_TMP
FINDSLOT_LOOP:
        LDA ZP_TMP
        CMP #DOS_MAX_FILES
        BCS FINDSLOT_NONE
        JSR ENTRYPTR
        LDY #0
        LDA (ZP_PTR2),Y
        BEQ FINDSLOT_FOUND
        INC ZP_TMP
        JMP FINDSLOT_LOOP
FINDSLOT_NONE:
        LDA #$FF
        RTS
FINDSLOT_FOUND:
        LDA ZP_TMP
        RTS

; ---------------------------------------------------------------------------
; Formatting
; ---------------------------------------------------------------------------

; DOSFORMAT: a blank volume named KESWARP. Nothing else on the disk survives,
; which is what FORMAT means.
DOSFORMAT:
        LDX #0
DOSFORMAT_VOL:
        LDA #0
        STA VOLBUF,X
        INX
        BNE DOSFORMAT_VOL
        ; The signature: HBDOS10, then a zero.
        LDA #'H'
        STA VOLBUF+0
        LDA #'B'
        STA VOLBUF+1
        LDA #'D'
        STA VOLBUF+2
        LDA #'O'
        STA VOLBUF+3
        LDA #'S'
        STA VOLBUF+4
        LDA #'1'
        STA VOLBUF+5
        LDA #'0'
        STA VOLBUF+6
        ; The name, space padded.
        LDX #0
DOSFORMAT_NAME:
        LDA FORMATNAME,X
        STA VOLBUF+8,X
        INX
        CPX #16
        BNE DOSFORMAT_NAME
        ; The sector count, 512.
        LDA #0
        STA VOLBUF+24
        LDA #2
        STA VOLBUF+25
        ; Sectors 0, 1 and 2 are the system's.
        LDA #%00000111
        STA VOLBUF+DOS_FREE_MAP
        JSR WRITEVOL
        BNE DOSFORMAT_ERR
        LDX #0
DOSFORMAT_DIR:
        LDA #0
        STA DIRBUF,X
        STA DIRBUF+256,X
        INX
        BNE DOSFORMAT_DIR
        JSR WRITEDIR
        BNE DOSFORMAT_ERR
        LDA #0
        STA ZP_DSKSTAT
        RTS
DOSFORMAT_ERR:
        LDA #ERR_DISK
        STA ZP_DSKSTAT
        RTS

FORMATNAME:
        .text "KESWARP          "

; ---------------------------------------------------------------------------
; Decimal output, for the directory listing
; ---------------------------------------------------------------------------

; PRDEC16: prints the unsigned 16-bit number in ZP_PTR, in decimal. The digits
; are collected first, because printing one character uses most of the KERNAL's
; zero page -- including ZP_PTR, where the number is.
PRDEC16:
        LDA #0
        STA ZP_TMP4             ; no digit has been worth printing yet
        LDX #0
        LDA #$27                ; ten thousand
        STA ZP_TMP
        LDA #$10
        STA ZP_TMP2
        JSR PRDIGIT
        LDA #$03                ; a thousand
        STA ZP_TMP
        LDA #$E8
        STA ZP_TMP2
        JSR PRDIGIT
        LDA #$64                ; a hundred
        STA ZP_TMP
        LDA #0
        STA ZP_TMP2
        JSR PRDIGIT
        LDA #$0A                ; ten
        STA ZP_TMP
        LDA #0
        STA ZP_TMP2
        JSR PRDIGIT
        LDA ZP_PTR              ; and the units
        ORA #$30
        STA PRBUF,X
        INX
        CPX #1
        BNE PRDEC16_OUT
        LDA #'0'                ; the number was zero and nothing was printed
        JMP CHROUT
PRDEC16_OUT:
        LDA #0                  ; a zero ends the digits, and Y survives a
        STA PRBUF,X             ; character going out while X does not
        LDY #0
PRDEC16_OUT_LOOP:
        LDA PRBUF,Y
        BEQ PRDEC16_OUT_DONE
        JSR CHROUT
        INY
        JMP PRDEC16_OUT_LOOP
PRDEC16_OUT_DONE:
        RTS

PRDIGIT:
        JSR PRDIV
        LDA ZP_TMP3
        BNE PRDIGIT_SHOW
        LDA ZP_TMP4
        BEQ PRDIGIT_DONE
PRDIGIT_SHOW:
        LDA #1
        STA ZP_TMP4
        LDA ZP_TMP3
        ORA #$30
        STA PRBUF,X
        INX
PRDIGIT_DONE:
        RTS

; PRDIV: ZP_PTR divided by the constant in ZP_TMP/ZP_TMP2. The quotient lands
; in ZP_TMP3 and the remainder stays in ZP_PTR.
PRDIV:
        LDA #0
        STA ZP_TMP3
PRDIV_LOOP:
        LDA ZP_PTR+1
        CMP ZP_TMP2
        BCC PRDIV_DONE
        BNE PRDIV_SUB
        LDA ZP_PTR
        CMP ZP_TMP
        BCC PRDIV_DONE
PRDIV_SUB:
        LDA ZP_PTR
        SEC
        SBC ZP_TMP
        STA ZP_PTR
        LDA ZP_PTR+1
        SBC ZP_TMP2
        STA ZP_PTR+1
        INC ZP_TMP3
        JMP PRDIV_LOOP
PRDIV_DONE:
        RTS

MAPBIT:
        .byte 1, 2, 4, 8, 16, 32, 64, 128
