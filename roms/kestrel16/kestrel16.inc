; SPDX-License-Identifier: MIT
;
; kestrel16.inc -- the Kestrel 16's machine constants, the same map the emulator decodes in
; include/kestrel/model1992/map.hpp. When one changes the other changes with it.

; The ROM banks.
KERNAL          = $00E000
KERNAL_SIZE     = $2000
BASIC           = $010000
BASIC_SIZE      = $10000
BANK_BASIC      = $01
BANK_KERNAL     = $00

; The cartridge: 128 KiB, of which the first 8 KiB answers in bank zero.
CART            = $00C000
CART_SIZE       = $020000
CART_ID         = CART+0    ; 'K','S','T','L', which the reset looks for
CART_VECTOR     = CART+4    ; and where it goes when it finds them

; Bank zero's RAM.
ZP              = $000000
STACK           = $000100
INBUF           = $000400
KEYBUF          = $000300
KEYBUF_SIZE     = 32
LINEBUF         = $000500
PROGSTART       = $000800
PROG_LIMIT      = $007E00       ; the program may not grow past here
RAMTOP          = $007F00

; The language's string space. Bank three is nobody else's -- the filesystem has
; bank two and the program has bank zero -- so a string variable can be a fixed
; slot there: a name always points at the same place, an assignment overwrites
; what was there, and nothing ever has to be collected.
STRINGS         = $030000
STR_SLOT        = 32            ; a length byte and thirty-one characters
STR_MAX         = 31
BANK_STR        = $03
STRTMP          = $030400       ; where two strings are joined: STR_MAX bytes
STRCHR          = $030430       ; the one character a CHR$ has
STRNUM          = $030438       ; the digits of a STR$: a sign and five more

; The arrays, in the same bank: a table of twenty-six entries, four bytes each
; -- where a letter's run starts and how many elements it has -- and a heap
; above the table that a DIM takes from and nothing gives back.
ARRAYS          = $030500       ; twenty-six entries, eight bytes each
ARR_TOP         = $0305D0       ; two bytes: the heap's first free offset
DEFNS           = $030600       ; twenty-six entries, four bytes each: where a
                                ; definition's text is, and which letter is its
DEFTEXT         = $030700       ; one definition's text, DEF_SIZE bytes each
DEF_SIZE        = 64
DEFGUARD        = $030D80       ; a byte: a definition is being read right now
DEFARG          = $030D81       ; and the letter it calls its argument by
DEFNSEL         = $030D82       ; and which of the twenty-six is being read
ARR_HEAP        = $1000         ; where the heap begins, as an offset in bank three
ARR_LIMIT       = $FF00         ; and the first offset past the end of it
ARR_MAX         = 4000          ; the longest run a DIM may ask for

; The tube: 80 x 50 cells, then one attribute byte each.
SCREEN          = $008000
ATTRS           = $008FA0
COLUMNS         = 80
ROWS            = 50
CELLS           = 4000

; The I/O page.
IO              = $00A000
IO_KBDDATA      = IO+$00
IO_KBDSTAT      = IO+$01
IO_KBDCLEAR     = IO+$02
IO_CURCOL       = IO+$03
IO_CURROW       = IO+$04
IO_INK          = IO+$05
IO_PAPER        = IO+$06
IO_BORDER       = IO+$07
IO_SOUND        = IO+$08
IO_FRAMELO      = IO+$09
IO_FRAMEHI      = IO+$0A
IO_FRAMEFLAG    = IO+$0B
IO_SECTORLO     = IO+$0C
IO_SECTORHI     = IO+$0D
IO_DISKCMD      = IO+$0E
IO_DISKSTAT     = IO+$0F
IO_ADDRLO       = IO+$10
IO_ADDRHI       = IO+$11
IO_ADDRBANK     = IO+$12
IO_COUNTLO      = IO+$13
IO_COUNTHI      = IO+$14
IO_RANDOM       = IO+$15
IO_LASTKEY      = IO+$16
IO_SCROLLED     = IO+$17
IO_CURSOR       = IO+$18
IO_TAPEDATA     = IO+$19
IO_TAPESTAT     = IO+$1A
IO_TAPECTRL     = IO+$1B

; The disk's commands and the bits its status byte carries.
DISK_READ       = 1
DISK_WRITE      = 2
DISK_FORMAT     = 3
DISK_QUERY      = 4
DISK_BUSY       = $01
DISK_ERROR      = $02
DISK_PRESENT    = $04
DISK_DONE       = $80

; The tape's control byte.
TAPE_PLAY       = 1
TAPE_RECORD     = 2
TAPE_STOP       = 3
TAPE_REWIND     = 4

; Colours, as the attribute byte packs them.
BLACK           = $0
BLUE            = $1
RED             = $2
MAGENTA         = $3
GREEN           = $4
CYAN            = $5
YELLOW          = $6
LIGHTGREY       = $7
DARKGREY        = $8
LIGHTBLUE       = $9
LIGHTRED        = $A
LIGHTMAGENTA    = $B
LIGHTGREEN      = $C
LIGHTCYAN       = $D
LIGHTYELLOW     = $E
WHITE           = $F

; Keys the machine names.
KEY_RETURN      = 13
KEY_DELETE      = 8
KEY_ESCAPE      = 27
KEY_LEFT        = $9D
KEY_RIGHT       = $1D

; Direct page. $00-$0F is scratch an expression may use; the rest belongs to
; whatever is running.
ZP_PTR          = $10           ; a 24-bit pointer, low byte first
ZP_PTR2         = $13
ZP_COL          = $16
ZP_ROW          = $17
ZP_OFFSET       = $18           ; 16-bit: row * 80 + column
ZP_INLEN        = $1A           ; 16-bit
ZP_TMP          = $1C           ; 16-bit
ZP_TMP2         = $1E           ; 16-bit
ZP_IRQCNT       = $20
ZP_KBHEAD       = $21
ZP_KBTAIL       = $22
ZP_KBCOUNT      = $23
ZP_BLINK        = $24
ZP_CHAR         = $25
ZP_SCRATCH      = $26
ZP_SCRATCH2     = $27
ZP_SCRATCH3     = $28
ZP_MUL1         = $29           ; 16-bit scratch, for the cursor arithmetic
ZP_MUL2         = $2B

; The filesystem's own direct page, above the language's. It has one because it
; is the only part of the machine that is neither the KERNAL nor a language: it
; has to keep pointers alive across calls to CHROUT, which is allowed to use
; every register the processor has.
ZP_DOSNM        = $66           ; 24-bit: the name the call is about, in bank zero
ZP_DOSFIRST     = $69           ; 16-bit: the first sector of the file DOSFIND found
ZP_DOSCOUNT     = $6B           ; 16-bit: the sectors it occupies, or the allocator's need
ZP_DOSIDX       = $6D           ; 16-bit: the directory entry under the head
ZP_DOSLEN       = $6F           ; 16-bit: bytes: a file's length, or a program's
ZP_DOSLEFT      = $71           ; 16-bit: a count that runs down
ZP_DOSSRC       = $73           ; 24-bit: where a move reads
ZP_DOSDST       = $76           ; 24-bit: where it writes
ZP_DOSSEC       = $79           ; 16-bit: the sector under the head
ZP_DOSSIZE      = $7B           ; 16-bit: how much of it this pass moves
ZP_DOSSCAN      = $7D           ; 16-bit: a sector the allocator is looking at
ZP_DOSOFF       = $7F           ; 16-bit: an offset into the directory
ZP_DOSCUR       = $81           ; 16-bit: an offset into whatever is being printed
ZP_DOSFLAG      = $83           ; a byte: what the last look found
ZP_DOSPTR       = $84           ; 24-bit: the buffer the filesystem is looking at

; The program store, which the language and the filesystem both walk: records
; from PROGSTART, a zero number at the end, and the address of that zero number
; in BASIC's ZP_PROGTOP -- which the DOS writes when it loads a program, so the
; language knows where its own program ends.
PROG_TOP        = $50           ; 16-bit, the same word BASIC calls ZP_PROGTOP

; The KERNAL's jump table: every service is a fixed address, which is how the
; language bank, and any program a person writes, reaches the machine.
K_CHROUT        = KERNAL+$0000
K_GETIN         = KERNAL+$0003
K_CHRIN         = KERNAL+$0006
K_CLS           = KERNAL+$0009
K_HOME          = KERNAL+$000C
K_PRINT         = KERNAL+$000F
K_SCROLL        = KERNAL+$0012
K_LOCATE        = KERNAL+$0015
K_GETLINE       = KERNAL+$0018
K_READSECT      = KERNAL+$001B
K_WRITESECT     = KERNAL+$001E
K_CURSORON      = KERNAL+$0021
K_DOSCATALOG    = KERNAL+$0024
K_DOSFIND       = KERNAL+$0027
K_DOSLOAD       = KERNAL+$002A
K_DOSSAVE       = KERNAL+$002D
K_DOSDELETE     = KERNAL+$0030
K_DOSSTATUS     = KERNAL+$0033
K_DOSFORMAT     = KERNAL+$0036
K_SETINK        = KERNAL+$0039
K_SETPAPER      = KERNAL+$003C
K_SOUND         = KERNAL+$003F
K_FRAMES        = KERNAL+$0042
K_MEMTOP        = KERNAL+$0045
K_PEEK          = KERNAL+$0048
K_POKE          = KERNAL+$004B
K_GETKEY        = KERNAL+$004E
K_NEWLINE       = KERNAL+$0051
K_TAPESAVE      = KERNAL+$0054
K_TAPELOAD      = KERNAL+$0057

; The long-call table, for the language in bank one: each entry is four
; bytes, JSR and RTL, so a caller reaches the same routines with JSL.
L_CHROUT      = KERNAL+$1F00+0*4
L_GETIN       = KERNAL+$1F00+1*4
L_CHRIN       = KERNAL+$1F00+2*4
L_CLS         = KERNAL+$1F00+3*4
L_HOME        = KERNAL+$1F00+4*4
L_PRINT       = KERNAL+$1F00+5*4
L_SCROLL      = KERNAL+$1F00+6*4
L_LOCATE      = KERNAL+$1F00+7*4
L_GETLINE     = KERNAL+$1F00+8*4
L_READSECT    = KERNAL+$1F00+9*4
L_WRITESECT   = KERNAL+$1F00+10*4
L_CURSORON    = KERNAL+$1F00+11*4
L_DOSCATALOG  = KERNAL+$1F00+12*4
L_DOSFIND     = KERNAL+$1F00+13*4
L_DOSLOAD     = KERNAL+$1F00+14*4
L_DOSSAVE     = KERNAL+$1F00+15*4
L_DOSDELETE   = KERNAL+$1F00+16*4
L_DOSSTATUS   = KERNAL+$1F00+17*4
L_DOSFORMAT   = KERNAL+$1F00+18*4
L_SETINK      = KERNAL+$1F00+19*4
L_SETPAPER    = KERNAL+$1F00+20*4
L_SOUND       = KERNAL+$1F00+21*4
L_FRAMES      = KERNAL+$1F00+22*4
L_MEMTOP      = KERNAL+$1F00+23*4
L_PEEKBYTE    = KERNAL+$1F00+24*4
L_POKEBYTE    = KERNAL+$1F00+25*4
L_GETKEY      = KERNAL+$1F00+26*4
L_NEWLINE     = KERNAL+$1F00+27*4
L_TAPESAVE    = KERNAL+$1F00+28*4
L_TAPELOAD    = KERNAL+$1F00+29*4
