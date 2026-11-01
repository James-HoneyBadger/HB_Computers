; SPDX-License-Identifier: MIT
;
; bz.inc -- what the ROM in the socket sees: BASIC's zero page, its stacks and
; the constants the interpreter shares between files.
;
; The KERNAL owns $00-$1F; the socket owns $20-$FF and the top of RAM.

; ---------------------------------------------------------------------------
; Zero page
; ---------------------------------------------------------------------------
B_IPTR      = $20       ; $20/$21  where the parser is in the current line
B_LINENO    = $22       ; $22/$23  the line being executed
B_CURLINE   = $24       ; $24/$25  its header in the program
B_TXTTAB    = $26       ; $26/$27  where the program starts
B_VARTAB    = $28       ; $28/$29  start of simple variables
B_ARYTAB    = $2A       ; $2A/$2B  start of the array area
B_STREND    = $2C       ; $2C/$2D  end of the array area
B_FRETOP    = $2E       ; $2E/$2F  the head of the array list, 0 when there
                        ; are none
B_MEMTOP    = $30       ; $30/$31  top of the memory a program may use
B_DATAPTR   = $32       ; $32/$33  where the next READ value is
B_DATALINE  = $4B       ; $4B/$4C  the line that READ is working through
B_FORSP     = $34
B_GOSUBSP   = $35
B_VALSP     = $36
B_TY        = $37       ; sign of a floating-point result
B_FAC       = $38       ; $38-$3C  the accumulator
B_ARG       = $3D       ; $3D-$41  the argument
B_TMP       = $42
B_TMP2      = $43
B_TMP3      = $44
B_SIGN      = $45
B_DIGITS    = $46
B_EXP       = $47
B_STRPTR    = $48       ; $48/$49
B_STRLEN    = $4A
B_STRLEN2   = $4D
B_INT       = $4E       ; $4E/$4F
B_INT2      = $50       ; $50/$51
B_TMPP      = $52       ; $52/$53
B_TMPP2     = $54       ; $54/$55
B_MODE      = $56       ; 0 at the prompt, 1 running a program
B_ERRNUM    = $57
B_CHAR      = $58
B_TRACE     = $59
B_FLAGS     = $5A       ; bit 0: a jump has been taken
B_NESTED    = $5B       ; how many FOR/NEXT loops deep we are
B_QUOTE     = $5C       ; inside a string literal
B_TEMP      = $5D
B_TEMP2     = $5E
B_TEMP3     = $5F
B_FPBUF     = $60       ; $60-$6F  the floating-point scratch block
B_SCR1      = $65       ; $65-$69  a five-byte number the series work holds
B_SCR2      = $6A       ; $6A-$6E  a second one
B_SCR3      = $8B       ; the third, shared with FSQR, which never runs inside
B_SCR4      = $A0       ; the fourth, inside the number buffer: formatting does
                        ; not happen while a series is being summed
B_SCR5      = $A5       ; the fifth, for TAN, which needs two angles at once
B_WORK      = $70       ; $70-$7F  the floating-point working registers
B_TEMPSTR   = $80       ; $80/$81
B_TEMPSTR2  = $82       ; $82/$83
B_LINEBUF   = $84       ; $84/$85  a line being assembled or found
B_STRLEN3   = $86
B_ERRFLAG   = $87
B_LOOPCNT   = $88       ; a counter the floating-point routines cannot touch
B_SPARE2    = $89
B_SPARE3    = $8A
B_SQRBUF    = $8B       ; $8B-$8F  the radicand FSQR holds, and TAN's cosine
B_ADDR      = $94       ; $94/$95, an address held across an expression
B_VNAME1    = $9B       ; a variable name, kept across the expression
B_VNAME2    = $9C       ; that assigns to it
B_VNAMET    = $9D
B_DATFLAG   = $9E       ; READ is partway through a DATA list
B_ONFLAG    = $9F       ; ON is choosing for a GOSUB rather than a GOTO
; Arrays work in a block of zero page of their own, above everything else and
; clear of the stack, because a subscript is an expression and will use every
; other scratch byte in the machine.
B_ARYPTR    = $C0       ; $C0/$C1  the array being indexed
B_ARYTMP    = $C2       ; $C2/$C3  the offset into its elements
B_ARYSUB    = $C4       ; $C4/$C5  a subscript
B_ARYSUM    = $C6       ; $C6/$C7  a product
B_ARYMUL    = $C8       ; an eight-bit multiplier
B_ARYIDX    = $C9       ; which subscript
B_ARYCNT    = $CA       ; a loop counter, or the type of the array in hand
B_ARYTYPE   = $DF       ; the type of the array being looked for
B_DIMS      = $CB       ; $CB-$D2  the sizes given to DIM, two bytes each
B_DIMN      = $D3       ; how many dimensions
B_DIMTOT    = $D4       ; $D4/$D5  how many elements, or how big the block is
B_DIMPTR    = $D6       ; $D6/$D7  the descriptor being filled
B_CONTPTR   = $D8       ; $D8/$D9  where CONT picks a stopped program up
B_CONTCUR   = $DA       ; $DA/$DB  the line it was on
B_CONTLINE  = $DC       ; $DC/$DD  its number
B_CANCONT   = $DE       ; a STOP is waiting to be continued
B_DEFEXPR   = $E0       ; $E0/$E1  the body of the FN being called
B_DEFP1     = $E2       ; its parameter
B_DEFP2     = $E3
DEFTAB      = $0720     ; four definitions of six bytes: name, parameter, body
DEFENT      = 6
DEFMAX      = 4
B_STRCHR    = $A0       ; a one-character string result, in the number buffer
B_NUMBUF    = $A0       ; $A0-$B7  a formatted number on its way to the tube
B_DIGITBUF  = $B8       ; $B8-$BF  the digits of a number being formatted
B_FEXP      = $88       ; the decimal exponent while formatting
B_FLAST     = $89       ; the last digit worth printing
B_FTMP      = $8A
B_FSIGN     = $8B       ; the sign while parsing
B_FDIG      = $8C       ; digits after the point while parsing
B_FPOINT    = $8D       ; a point has been seen
B_FSEEN     = $8E       ; a digit has been seen
B_FCOUNT    = $8F       ; the exponent being accumulated

; ---------------------------------------------------------------------------
; Stacks, at the top of RAM where nothing grows
; ---------------------------------------------------------------------------
VALSTK      = $7F00     ; 12 levels of 6 bytes: type, then five bytes
VALSTK_MAX  = 12
VALSIZE     = 6
FORSTK      = $7F50     ; 7 loops of 16 bytes
FORSTK_MAX  = 7
FORSIZE     = 16
GOSUBSTK    = $7FC0     ; 16 frames of 4 bytes: the line and the position in it
GOSUB_MAX   = 16
GOSUBSIZE   = 4
MEMTOP      = $7EFF     ; the highest byte a program may use

; ---------------------------------------------------------------------------
; Program text
; ---------------------------------------------------------------------------
PROGSTART   = $0801
LINELINK    = 0         ; next line, two bytes
LINENUM     = 2         ; the line number, two bytes
LINETEXT    = 4         ; the text, zero terminated

; ---------------------------------------------------------------------------
; Value types, as they sit on the value stack
; ---------------------------------------------------------------------------
T_NUM       = 0
T_STR       = 1

; ---------------------------------------------------------------------------
; Tokens
; ---------------------------------------------------------------------------
TOK_END     = 0

; ---------------------------------------------------------------------------
; Error numbers
; ---------------------------------------------------------------------------
E_NEXT_WITHOUT_FOR  = 1
E_SYNTAX            = 2
E_RETURN_WITHOUT_GOSUB = 3
E_OUT_OF_DATA       = 4
E_IF_WITHOUT_THEN    = 5
E_REDIM             = 6
E_DIVISION_BY_ZERO  = 7
E_OUT_OF_MEMORY     = 8
E_UNDEF_STATEMENT   = 9
E_BAD_SUBSCRIPT     = 10
E_ILLEGAL_QUANTITY  = 11
E_BAD_FILENAME      = 12
E_FILE_NOT_FOUND    = 13
E_DISK              = 14
E_BREAK             = 15
E_OVERFLOW          = 16
E_UNDEF_FUNCTION    = 17
E_BAD_DATA          = 18
E_STRING_TOO_LONG   = 19
E_CANT_CONTINUE     = 20
E_NO_FILE           = 21
E_NOT_YET           = 22
E_TYPE_MISMATCH     = 23

; ---------------------------------------------------------------------------
; Control codes used by the printing code
; ---------------------------------------------------------------------------
CC_QUOTE_TOGGLE = 18
