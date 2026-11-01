; SPDX-License-Identifier: MIT
;
; monitor.s -- the Kestrel machine-code monitor.
;
; The other ROM a 1982 machine shipped with. Where BASIC talks to a person in
; numbers and strings, this talks to the processor in bytes: examine memory,
; change it, start it, look at the registers. It is what you use when the
; program you are debugging is the one you are writing in hex.
;
; Commands (the prompt is a full stop):
;
;       M aaaa [nn]     examine nn bytes (default 64) from aaaa
;       : aaaa bb ...   deposit bytes
;       G [aaaa]        go: jump to aaaa and run
;       R               registers
;       S aaaa bbbb     save the block to tape, with a four-byte header
;       L               load a block from tape, to the address in its header
;
; Everything is hexadecimal, which is the point of a monitor.

        .include "kestrel.inc"

M_PTR       = $20       ; $20/$21, where the parser is in the line
M_WORD      = $22       ; $22/$23, the number just parsed
M_ADDR      = $24       ; $24/$25, the address being examined or saved
M_COUNT     = $26
M_TMP       = $27
M_TMP2      = $28
M_REGA      = $29
M_REGX      = $2A
M_REGY      = $2B
M_REGP      = $2C
M_REGS      = $2D
M_PCL       = $2E
M_PCH       = $2F

        .org SOCKET

COLD_START_ENTRY:
        JMP MON_COLD
WARM_START_ENTRY:
        JMP MON_WARM

MON_COLD:
        LDA #<MON_BANNER
        STA ZP_STRPTR
        LDA #>MON_BANNER
        STA ZP_STRPTR+1
        JSR K_PRINT
MON_WARM:
        LDA #<MON_PROMPT
        STA ZP_STRPTR
        LDA #>MON_PROMPT
        STA ZP_STRPTR+1
        JSR K_PRINT
        JSR K_GETLINE
        LDA #<INBUF
        STA M_PTR
        LDA #>INBUF
        STA M_PTR+1
        JSR SKIP
        JSR GETCH
        CMP #0
        BEQ MON_WARM
        JSR ADV                 ; past the command letter
        CMP #'M'
        BEQ MON_M
        CMP #':'
        BEQ MON_DEPOSIT
        CMP #'G'
        BEQ MON_GO
        CMP #'R'
        BEQ MON_REGISTERS
        CMP #'S'
        BEQ MON_SAVE
        CMP #'L'
        BEQ MON_LOAD
        JMP MON_BAD
MON_BAD:
        LDA #<MON_WHAT
        STA ZP_STRPTR
        LDA #>MON_WHAT
        STA ZP_STRPTR+1
        JSR K_PRINT
        JMP MON_WARM

; --- plumbing --------------------------------------------------------------

GETCH:                  ; A = the character at (M_PTR)
        LDY #0
        LDA (M_PTR),Y
        RTS

ADV:
        INC M_PTR
        BNE ADV_DONE
        INC M_PTR+1
ADV_DONE:
        RTS

SKIP:
        JSR GETCH
        CMP #' '
        BNE SKIP_DONE
        JSR ADV
        JMP SKIP
SKIP_DONE:
        RTS

; PARSEHEX: a four-digit hexadecimal number at (M_PTR) into M_WORD.
; Carry set when there was one.
PARSEHEX:
        LDA #0
        STA M_WORD
        STA M_WORD+1
        STA M_TMP2
PARSEHEX_LOOP:
        JSR GETCH
        JSR HEXVAL
        BCS PARSEHEX_DONE
        ; M_WORD = M_WORD * 16 + digit
        ASL M_WORD
        ROL M_WORD+1
        ASL M_WORD
        ROL M_WORD+1
        ASL M_WORD
        ROL M_WORD+1
        ASL M_WORD
        ROL M_WORD+1
        LDA M_WORD
        ORA M_TMP
        STA M_WORD
        INC M_TMP2
        JSR ADV
        JMP PARSEHEX_LOOP
PARSEHEX_DONE:
        LDA M_TMP2
        BEQ PARSEHEX_NONE
        CLC
        RTS
PARSEHEX_NONE:
        SEC
        RTS

; HEXVAL: A = the value of hex character A, carry set when it is not one.
HEXVAL:
        PHA
        JSR UP
        STA M_TMP
        PLA
        CMP #'0'
        BCC HEXVAL_BAD
        CMP #'9'+1
        BCS HEXVAL_ALPHA
        SEC
        SBC #'0'
        STA M_TMP
        CLC
        RTS
HEXVAL_ALPHA:
        CMP #'A'
        BCC HEXVAL_BAD
        CMP #'F'+1
        BCS HEXVAL_BAD
        SEC
        SBC #'A'-10
        STA M_TMP
        CLC
        RTS
HEXVAL_BAD:
        SEC
        RTS

UP:
        CMP #'a'
        BCC UP_DONE
        CMP #'z'+1
        BCS UP_DONE
        AND #$5F
UP_DONE:
        RTS

PRHEX:                  ; A printed as two hex digits
        PHA
        LSR A
        LSR A
        LSR A
        LSR A
        JSR PRNIBBLE
        PLA
        AND #$0F
PRNIBBLE:
        CMP #10
        BCC PRNIBBLE_DIGIT
        CLC
        ADC #'A'-10
        JMP K_CHROUT
PRNIBBLE_DIGIT:
        CLC
        ADC #'0'
        JMP K_CHROUT

PRWORD:                 ; M_WORD printed as four hex digits
        LDA M_WORD+1
        JSR PRHEX
        LDA M_WORD
        JMP PRHEX

; --- M: examine ------------------------------------------------------------

MON_M:
        JSR SKIP
        JSR PARSEHEX
        BCS MON_M_DEFAULT
        LDA M_WORD
        STA M_ADDR
        LDA M_WORD+1
        STA M_ADDR+1
        JSR SKIP
        JSR PARSEHEX
        BCS MON_M_SIXTYFOUR
        LDA M_WORD
        STA M_COUNT
        JMP MON_M_DUMP
MON_M_DEFAULT:
        LDA #0
        STA M_ADDR
        LDA #0
        STA M_ADDR+1
MON_M_SIXTYFOUR:
        LDA #64
        STA M_COUNT
MON_M_DUMP:
        LDA #0
        STA M_TMP              ; the column within the line
MON_M_LINE:
        LDA M_TMP
        BNE MON_M_SPACE
        LDA M_ADDR+1
        JSR PRHEX
        LDA M_ADDR
        JSR PRHEX
        LDA #':'
        JSR K_CHROUT
        LDA #' '
        JSR K_CHROUT
MON_M_SPACE:
        LDA M_COUNT
        BEQ MON_M_END
        LDY #0
        LDA (M_ADDR),Y
        JSR PRHEX
        LDA #' '
        JSR K_CHROUT
        INC M_ADDR
        BNE MON_M_SKIP
        INC M_ADDR+1
MON_M_SKIP:
        DEC M_COUNT
        INC M_TMP
        LDA M_TMP
        CMP #8
        BNE MON_M_LINE
        LDA #CC_RETURN
        JSR K_CHROUT
        JMP MON_M_DUMP
MON_M_END:
        LDA #CC_RETURN
        JSR K_CHROUT
        JMP MON_WARM

; --- : deposit -------------------------------------------------------------

MON_DEPOSIT:
        JSR SKIP
        JSR PARSEHEX
        BCS MON_BAD
        LDA M_WORD
        STA M_ADDR
        LDA M_WORD+1
        STA M_ADDR+1
MON_DEP_LOOP:
        JSR SKIP
        JSR PARSEHEX
        BCS MON_DEP_DONE
        LDA M_WORD
        LDY #0
        STA (M_ADDR),Y
        INC M_ADDR
        BNE MON_DEP_LOOP
        INC M_ADDR+1
        JMP MON_DEP_LOOP
MON_DEP_DONE:
        JMP MON_WARM

; --- G: go -----------------------------------------------------------------

MON_GO:
        JSR SKIP
        JSR PARSEHEX
        BCS MON_GO_HERE
        LDA M_WORD
        STA M_ADDR
        LDA M_WORD+1
        STA M_ADDR+1
        ; the return address on the stack is the monitor's own prompt, so an
        ; RTS in the program comes back here
        LDA #>(MON_WARM-1)
        PHA
        LDA #<(MON_WARM-1)
        PHA
        JMP (M_ADDR)
MON_GO_HERE:
        LDA #>(MON_WARM-1)
        PHA
        LDA #<(MON_WARM-1)
        PHA
        JMP (M_ADDR)

; --- R: registers ----------------------------------------------------------

MON_REGISTERS:
        ; The registers are read here, not through BRK: the vectors at $FFFA
        ; belong to the KERNAL, so a ROM in the socket cannot claim them.
        STA M_REGA
        STX M_REGX
        STY M_REGY
        PHP
        PLA
        STA M_REGP
        TSX
        STX M_REGS

        LDA #<MON_LABEL_A
        STA ZP_STRPTR
        LDA #>MON_LABEL_A
        STA ZP_STRPTR+1
        JSR K_PRINT
        LDA M_REGA
        JSR PRHEX
        LDA #<MON_LABEL_X
        STA ZP_STRPTR
        LDA #>MON_LABEL_X
        STA ZP_STRPTR+1
        JSR K_PRINT
        LDA M_REGX
        JSR PRHEX
        LDA #<MON_LABEL_Y
        STA ZP_STRPTR
        LDA #>MON_LABEL_Y
        STA ZP_STRPTR+1
        JSR K_PRINT
        LDA M_REGY
        JSR PRHEX
        LDA #<MON_LABEL_P
        STA ZP_STRPTR
        LDA #>MON_LABEL_P
        STA ZP_STRPTR+1
        JSR K_PRINT
        LDA M_REGP
        JSR PRHEX
        LDA #<MON_LABEL_S
        STA ZP_STRPTR
        LDA #>MON_LABEL_S
        STA ZP_STRPTR+1
        JSR K_PRINT
        LDA M_REGS
        JSR PRHEX
        LDA #CC_RETURN
        JSR K_CHROUT
        JMP MON_WARM

; --- S: save a block to tape ----------------------------------------------

MON_SAVE:
        JSR SKIP
        JSR PARSEHEX
        BCS MON_BAD
        LDA M_WORD
        STA M_ADDR
        LDA M_WORD+1
        STA M_ADDR+1
        JSR SKIP
        JSR PARSEHEX
        BCS MON_BAD
        LDA M_WORD
        SEC
        SBC M_ADDR
        STA M_COUNT
        LDA M_WORD+1
        SBC M_ADDR+1
        STA M_COUNT+1
        LDA #2
        STA IO_TAPECONTROL      ; record
        LDA M_ADDR+1
        JSR TAPEW
        LDA M_ADDR
        JSR TAPEW
        LDA M_COUNT+1
        JSR TAPEW
        LDA M_COUNT
        JSR TAPEW
MON_SAVE_LOOP:
        LDA M_COUNT
        ORA M_COUNT+1
        BEQ MON_SAVE_DONE
        LDY #0
        LDA (M_ADDR),Y
        JSR TAPEW
        INC M_ADDR
        BNE MON_SAVE_SKIP
        INC M_ADDR+1
MON_SAVE_SKIP:
        LDA M_COUNT
        BNE MON_SAVE_DEC
        DEC M_COUNT+1
MON_SAVE_DEC:
        DEC M_COUNT
        JMP MON_SAVE_LOOP
MON_SAVE_DONE:
        LDA #3
        STA IO_TAPECONTROL      ; motor off
        LDA #<MON_SAVED
        STA ZP_STRPTR
        LDA #>MON_SAVED
        STA ZP_STRPTR+1
        JSR K_PRINT
        JMP MON_WARM

TAPEW:
        STA IO_TAPEDATA
        RTS

TAPER:
        LDA IO_TAPEDATA
        RTS

; --- L: load a block from tape --------------------------------------------

MON_LOAD:
        LDA IO_TAPESTAT
        AND #$80
        BEQ MON_LOAD_NONE
        LDA #4
        STA IO_TAPECONTROL      ; rewind
        LDA #1
        STA IO_TAPECONTROL      ; play
        JSR TAPER
        STA M_ADDR+1
        JSR TAPER
        STA M_ADDR
        JSR TAPER
        STA M_COUNT+1
        JSR TAPER
        STA M_COUNT
MON_LOAD_LOOP:
        LDA M_COUNT
        ORA M_COUNT+1
        BEQ MON_LOAD_DONE
        JSR TAPER
        LDY #0
        STA (M_ADDR),Y
        INC M_ADDR
        BNE MON_LOAD_SKIP
        INC M_ADDR+1
MON_LOAD_SKIP:
        LDA M_COUNT
        BNE MON_LOAD_DEC
        DEC M_COUNT+1
MON_LOAD_DEC:
        DEC M_COUNT
        JMP MON_LOAD_LOOP
MON_LOAD_DONE:
        LDA #3
        STA IO_TAPECONTROL
        LDA #<MON_LOADED
        STA ZP_STRPTR
        LDA #>MON_LOADED
        STA ZP_STRPTR+1
        JSR K_PRINT
        JMP MON_WARM
MON_LOAD_NONE:
        LDA #<MON_NOTAPE
        STA ZP_STRPTR
        LDA #>MON_NOTAPE
        STA ZP_STRPTR+1
        JSR K_PRINT
        JMP MON_WARM

; --- text ------------------------------------------------------------------

MON_BANNER:
        .byte CC_CLS
        .text "*** KESTREL MONITOR V1.0 ***"
        .byte 13
        .text "M aaaa [nn]  : aaaa bb  G aaaa  R"
        .byte 13
        .text "S aaaa bbbb (to tape)  L (from tape)"
        .byte 13, 13, 0

MON_PROMPT:
        .text "."
        .byte 0

MON_WHAT:
        .text "?"
        .byte 13, 0

MON_LABEL_A:
        .text "A="
        .byte 0
MON_LABEL_X:
        .text " X="
        .byte 0
MON_LABEL_Y:
        .text " Y="
        .byte 0
MON_LABEL_P:
        .text " P="
        .byte 0
MON_LABEL_S:
        .text " S="
        .byte 0
MON_LABEL_PC:
        .text " PC="
        .byte 0

MON_SAVED:
        .text "saved"
        .byte 13, 0

MON_LOADED:
        .text "loaded"
        .byte 13, 0

MON_NOTAPE:
        .text "no tape"
        .byte 13, 0

; A ROM in the socket has no vectors of its own: the KERNAL's reset jumps to
; $A000 and its BREAK handler to $A003, and the monitor's frame handler is
; entered through the KERNAL's IRQ vector -- see MON_IRQ above, which the
; KERNAL calls because the monitor installs itself there.
