; SPDX-License-Identifier: MIT
;
; kestrel.inc -- the Kestrel's hardware and KERNAL, as both ROMs see it.
;
; Included by every ROM in the socket. The addresses here are the machine's, not
; an implementation detail: BASIC calls the KERNAL through them, and so does a
; program that pokes a register.
;
; ---------------------------------------------------------------------------
; The map
; ---------------------------------------------------------------------------
;   $0000-$00FF  zero page          KERNAL: $02-$1F, ROM in the socket: $20-$FF
;   $0100-$01FF  processor stack
;   $0200-$02FF  input buffer (one line, 255 bytes)
;   $0300-$07FF  system workspace
;   $0801-$7FFF  program, variables and free memory
;   $8000-$83E7  screen, 40 x 25, one ASCII code per cell
;   $8400-$87E7  colour, one (ink<<4 | paper) byte per cell
;   $8800-$88FF  I/O registers
;   $8900-$9FFF  nothing responds
;   $A000-$DFFF  the ROM in the socket
;   $E000-$FFFF  KERNAL, with its vectors in the top six bytes

; ---------------------------------------------------------------------------
; Memory
; ---------------------------------------------------------------------------
RAM_TOP     = $7FFF
SCREEN      = $8000
CART        = $9000     ; a 4 KiB cartridge, when one is fitted
CART_SIZE   = $1000
ATTRS       = $8400
IO          = $8800
SOCKET      = $A000
KERNAL      = $E000

COLS        = 40
ROWS        = 25
SCREEN_SIZE = COLS*ROWS

; Where a ROM in the socket begins. Every language ROM answers these two
; addresses: the KERNAL's reset jumps to COLD_START, BREAK to WARM_START.
COLD_START  = SOCKET+0
WARM_START  = SOCKET+3

; ---------------------------------------------------------------------------
; I/O registers
; ---------------------------------------------------------------------------
IO_KBDDATA  = IO+$00    ; read: next key, or 0
IO_KBDSTAT  = IO+$01    ; bit 7: a key is waiting
IO_KBDCLR   = IO+$02    ; write: empty the buffer
IO_CURCOL   = IO+$03
IO_CURROW   = IO+$04
IO_INK      = IO+$05
IO_PAPER    = IO+$06
IO_BORDER   = IO+$07
IO_SOUND    = IO+$08
IO_FRAMELO  = IO+$09
IO_FRAMEHI  = IO+$0A
IO_FRAMEFLAG= IO+$0B    ; write 0 to acknowledge a frame
IO_DSKL     = IO+$0C
IO_DSKH     = IO+$0D
IO_DCMD     = IO+$0E    ; 1 read, 2 write, 3 sector count
IO_DSTAT    = IO+$0F    ; bit 7: the last command failed
IO_DBUFL    = IO+$10
IO_DBUFH    = IO+$11
IO_DCNTL    = IO+$12
IO_DCNTH    = IO+$13
IO_RANDOM   = IO+$14
IO_LASTKEY  = IO+$15
IO_SCROLLED = IO+$16    ; set when the screen scrolled; write 0 to clear
IO_CURSOR   = IO+$17    ; 1 while the cursor is lit
IO_TAPEDATA = IO+$18    ; read: the next byte on the tape; write: put one there
IO_TAPESTAT = IO+$19    ; bit 7 present, 6 playing, 5 recording, 4 data left
IO_TAPECONTROL = IO+$1A ; 1 play, 2 record, 3 stop, 4 rewind

; Colours, as POKE writes them: the low nibble of an attribute is the paper and
; the high nibble the ink.
BLACK       = 0
WHITE       = 1
RED         = 2
CYAN        = 3
PURPLE      = 4
GREEN       = 5
BLUE        = 6
YELLOW      = 7
ORANGE      = 8
BROWN       = 9
LIGHTRED    = 10
DARKGREY    = 11
GREY        = 12
LIGHTGREEN  = 13
LIGHTBLUE   = 14
LIGHTGREY   = 15

; ---------------------------------------------------------------------------
; Control codes CHROUT understands
; ---------------------------------------------------------------------------
CC_BACKSPACE = 8
CC_RETURN    = 13
CC_CLS       = 147
CC_HOME      = 19

; ---------------------------------------------------------------------------
; Zero page
; ---------------------------------------------------------------------------
ZP_TMPX     = $00       ; KERNAL scratch, used to preserve X across CHROUT
ZP_TMPY     = $01       ; the same for Y
ZP_TMP      = $02
ZP_TMP2     = $03
ZP_TMP3     = $04
ZP_TMP4     = $05
ZP_PTR      = $06       ; $06/$07
ZP_PTR2     = $08       ; $08/$09
ZP_STRPTR   = $0A       ; $0A/$0B
ZP_SCRPTR   = $0C       ; $0C/$0D
ZP_INPTR    = $0E       ; $0E/$0F
ZP_COL      = $10
ZP_ROW      = $11
ZP_CHR      = $12
ZP_BLINK    = $13
ZP_CURSOR   = $14
ZP_INLEN    = $15
ZP_ENTRY    = $16       ; the catalog's place in the directory
ZP_IRQCNT   = $17
ZP_DSKPTR   = $18       ; $18/$19, the DOS transfer pointer
ZP_DSKSTAT  = $1A
ZP_DSKEND   = $1B       ; $1B/$1C, the end of a SAVE or of a LOAD
ZP_KBHEAD   = $1D
ZP_KBTAIL   = $1E
ZP_KBCOUNT  = $1F

INBUF       = $0200
INBUF_MAX   = 250
KEYBUF      = $0700     ; 16 keys, filled by the interrupt handler
KEYBUF_MASK = 15

SAVEWORK    = $0718     ; the SAVE's own bookkeeping, in RAM where a store
SAVEENTRY   = SAVEWORK   ; takes effect: the entry, the first sector and the
SAVEFIRST   = SAVEWORK+2 ; one just written
SAVEPREV    = SAVEWORK+4
SAVETOT     = SAVEWORK+6 ; how long the file is, before the pointers move

DOSNAME     = $0710     ; the name a DOS call is looking for: 8, space padded.
                        ; It sits after the keyboard buffer because DIRBUF
                        ; runs from $0300 to $04FF and a directory read would
                        ; otherwise write over the name it was told to find.
SAVELEN     = $02FB     ; $02FB/$02FC, bytes still to write during a SAVE
SAVEPOS     = $02FD     ; $02FD/$02FE, where the SAVE has got to
DIRBUF      = $0300     ; both directory sectors, read and written as a unit
DIRBUF_SIZE = 512
VOLBUF      = $0500     ; the volume header, with the free-sector map
CHUNKBUF    = $0600     ; one sector of file data on its way through
PRBUF       = $0740     ; five digits, on their way out one at a time: printing a
                        ; character uses every zero page byte the KERNAL has

; ---------------------------------------------------------------------------
; KERNAL entry points. The jump table at $E000 is this list in the same order:
; a byte added here and not there would move every call in every ROM, so the
; table is written by hand and `kestrel_machine_tests` calls each entry.
; ---------------------------------------------------------------------------
K_CHROUT    = KERNAL+$00    ; A = character; X and Y preserved
K_GETIN     = KERNAL+$03    ; -> A = key, or 0
K_CHRIN     = KERNAL+$06    ; waits -> A = key
K_CLS       = KERNAL+$09
K_HOME      = KERNAL+$0C
K_PRINT     = KERNAL+$0F    ; prints the zero-terminated string at ZP_STRPTR
K_SCROLL    = KERNAL+$12
K_LOCATE    = KERNAL+$15    ; X = column, Y = row
K_GETLINE   = KERNAL+$18    ; -> ZP_INLEN, line in INBUF
K_READSECT  = KERNAL+$1B    ; A = sector lo, X = sector hi, ZP_PTR = buffer
K_WRITESECT = KERNAL+$1E
K_CURSORON  = KERNAL+$21    ; A = 1 solid cursor, 0 no cursor
K_DOSCATALOG= KERNAL+$24
K_DOSFIND   = KERNAL+$27    ; name at DOSNAME -> A = index or $FF
K_DOSLOAD   = KERNAL+$2A    ; ZP_DSKPTR = where -> ZP_DSKPTR = end, ZP_DSKEND = end
K_DOSSAVE   = KERNAL+$2D    ; ZP_DSKPTR = start, ZP_DSKEND = end
K_DOSDELETE = KERNAL+$30
K_DOSSTATUS = KERNAL+$33    ; -> A = 0 or an error code
K_FORMAT    = KERNAL+$36

; ---------------------------------------------------------------------------
; DOS constants
; ---------------------------------------------------------------------------
DOS_VOLUME      = 0
DOS_DIR_FIRST   = 1
DOS_DIR_SECTORS = 2
DOS_FIRST_DATA  = 3
DOS_MAX_FILES   = 32
DOS_ENTRY_SIZE  = 16
DOS_NAME_LEN    = 8
DOS_CHUNK       = 252
DOS_FREE_MAP    = 32        ; 64 bytes in the volume header: one bit per sector

; DOS error codes
ERR_NONE        = 0
ERR_NO_DISK     = 1
ERR_NOT_FOUND   = 2
ERR_NO_ROOM     = 3
ERR_DISK        = 4
