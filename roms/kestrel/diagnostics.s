; SPDX-License-Identifier: MIT
;
; diagnostics.s -- Kestrel Diagnostics V1.0, the ROM a service department used.
;
; The third thing a dealer put in the socket. It is not a language and it is not
; for writing programs with: it is what you fit when a machine comes back and
; nobody knows why. It tests the three things that fail -- memory, the tube and
; the keyboard -- and it says which address went wrong, which is the only useful
; thing a test of memory can say. What it prints is what a shop wrote on the job
; sheet:
;
;       RAM  $0800-$7FFF  FAIL AT 1A2C
;
; Everything it reports is measured. Memory is filled with two patterns and read
; back; the tube is filled and read back through the same memory programs use,
; which is put away and restored so the report stays on the screen; and the
; keyboard is the keys a person presses, with their codes. The sound chip cannot
; be read back on this machine, so that test plays a note and asks you to listen
; rather than pretending to pass.

        .include "kestrel.inc"

D_SRC       = $20       ; $20/$21, where a block is being read from
D_DST       = $22       ; $22/$23, and where it is going
D_COUNT     = $24       ; $24/$25, how many bytes are left in it
D_ADDR      = $26       ; $26/$27, the first byte that came back wrong
D_PAT       = $28       ; the pattern a test writes
D_TMP       = $29
D_WAIT      = $2A       ; $2A/$2B, the note's length

DIAG_RAM_START = $0800  ; below this is the KERNAL's own workspace: the input
                        ; buffer, the keyboard buffer, the DOS buffers, the stack
DIAG_SAVE      = $1000  ; where the tube goes while it is being tested

        .org SOCKET

COLD_START_ENTRY:
        JMP DIAG_RUN
WARM_START_ENTRY:
        JMP DIAG_RUN

; ---------------------------------------------------------------------------
; The series of tests
; ---------------------------------------------------------------------------
DIAG_RUN:
        LDA #<TXT_BANNER
        LDX #>TXT_BANNER
        JSR SAY
        JSR TEST_RAM
        JSR TEST_TUBE
        JSR TEST_SOUND
        JMP TEST_KEYS

; ---------------------------------------------------------------------------
; Memory: two patterns, written and read back
; ---------------------------------------------------------------------------
TEST_RAM:
        LDA #<TXT_RAM
        LDX #>TXT_RAM
        JSR SAY
        LDA #$55
        JSR RAM_CHECK
        BCS TEST_RAM_BAD
        LDA #$AA
        JSR RAM_CHECK
        BCS TEST_RAM_BAD
        LDA #<TXT_OK
        LDX #>TXT_OK
        JSR SAY
        JMP END_OF_LINE
TEST_RAM_BAD:
        LDA #<TXT_FAIL
        LDX #>TXT_FAIL
        JSR SAY
        LDA D_ADDR
        STA D_SRC
        LDA D_ADDR+1
        STA D_SRC+1
        JSR PRINT_ADDRESS
        JMP END_OF_LINE

; RAM_CHECK: fill the testable RAM with A and read it back. Carry set when a
; byte did not come back, and D_ADDR is the address of the first one.
RAM_CHECK:
        STA D_PAT
        LDA #<DIAG_RAM_START
        STA D_DST
        LDA #>DIAG_RAM_START
        STA D_DST+1
        SEC                     ; up to and including RAM_TOP
        LDA #<(RAM_TOP-DIAG_RAM_START+1)
        STA D_COUNT
        LDA #>(RAM_TOP-DIAG_RAM_START+1)
        STA D_COUNT+1
        JSR BLOCK_FILL
        LDA #<DIAG_RAM_START
        STA D_DST
        LDA #>DIAG_RAM_START
        STA D_DST+1
        LDA #<(RAM_TOP-DIAG_RAM_START+1)
        STA D_COUNT
        LDA #>(RAM_TOP-DIAG_RAM_START+1)
        STA D_COUNT+1
        JMP BLOCK_VERIFY

; ---------------------------------------------------------------------------
; The tube: put away, filled, read back, put back
; ---------------------------------------------------------------------------
TEST_TUBE:
        LDA #<TXT_TUBE
        LDX #>TXT_TUBE
        JSR SAY
        JSR TUBE_SAVE
        JSR TUBE_CHECK
        LDA #0
        ROL A                   ; the verdict out of the way of the restore
        STA D_TMP
        JSR TUBE_RESTORE
        LDA D_TMP
        BNE TEST_TUBE_BAD
        LDA #<TXT_OK
        LDX #>TXT_OK
        JSR SAY
        JMP END_OF_LINE
TEST_TUBE_BAD:
        LDA #<TXT_FAIL
        LDX #>TXT_FAIL
        JSR SAY
        JMP END_OF_LINE
TEST_TUBE_OK:
        LDA #<TXT_OK
        LDX #>TXT_OK
        JSR SAY
        JMP END_OF_LINE

; The tube is two blocks of a thousand bytes -- the characters and the colours
; -- with a gap between them that answers to nobody, so each is its own block.
TUBE_SAVE:
        LDA #<SCREEN
        STA D_SRC
        LDA #>SCREEN
        STA D_SRC+1
        LDA #<DIAG_SAVE
        STA D_DST
        LDA #>DIAG_SAVE
        STA D_DST+1
        JSR TUBE_COUNT
        JSR BLOCK_COPY
        LDA #<ATTRS
        STA D_SRC
        LDA #>ATTRS
        STA D_SRC+1
        LDA #<(DIAG_SAVE+SCREEN_SIZE)
        STA D_DST
        LDA #>(DIAG_SAVE+SCREEN_SIZE)
        STA D_DST+1
        JSR TUBE_COUNT
        JMP BLOCK_COPY

TUBE_RESTORE:
        LDA #<DIAG_SAVE
        STA D_SRC
        LDA #>DIAG_SAVE
        STA D_SRC+1
        LDA #<SCREEN
        STA D_DST
        LDA #>SCREEN
        STA D_DST+1
        JSR TUBE_COUNT
        JSR BLOCK_COPY
        LDA #<(DIAG_SAVE+SCREEN_SIZE)
        STA D_SRC
        LDA #>(DIAG_SAVE+SCREEN_SIZE)
        STA D_SRC+1
        LDA #<ATTRS
        STA D_DST
        LDA #>ATTRS
        STA D_DST+1
        JSR TUBE_COUNT
        JMP BLOCK_COPY

TUBE_COUNT:
        LDA #<SCREEN_SIZE
        STA D_COUNT
        LDA #>SCREEN_SIZE
        STA D_COUNT+1
        RTS

TUBE_CHECK:
        LDA #$A5
        STA D_PAT
        LDA #<SCREEN
        STA D_DST
        LDA #>SCREEN
        STA D_DST+1
        JSR TUBE_COUNT
        JSR BLOCK_FILL
        LDA #<SCREEN
        STA D_DST
        LDA #>SCREEN
        STA D_DST+1
        JSR TUBE_COUNT
        JSR BLOCK_VERIFY
        BCS TUBE_CHECK_DONE
        LDA #$5A
        STA D_PAT
        LDA #<ATTRS
        STA D_DST
        LDA #>ATTRS
        STA D_DST+1
        JSR TUBE_COUNT
        JSR BLOCK_FILL
        LDA #<ATTRS
        STA D_DST
        LDA #>ATTRS
        STA D_DST+1
        JSR TUBE_COUNT
        JMP BLOCK_VERIFY
TUBE_CHECK_DONE:
        SEC
        RTS

; ---------------------------------------------------------------------------
; Sound: nothing to read back, so a note and a wait
; ---------------------------------------------------------------------------
TEST_SOUND:
        LDA #<TXT_SOUND
        LDX #>TXT_SOUND
        JSR SAY
        LDA #$60
        STA IO_SOUND
        JSR NOTE_WAIT
        LDA #0
        STA IO_SOUND
        LDA #<TXT_LISTEN
        LDX #>TXT_LISTEN
        JSR SAY
        JMP END_OF_LINE

; NOTE_WAIT: long enough for a person to hear the note.
NOTE_WAIT:
        LDA #0
        STA D_TMP
        STA D_WAIT
        STA D_WAIT+1
NOTE_WAIT_LOOP:
        LDA D_WAIT+1
        CMP #1
        BEQ NOTE_WAIT_DONE
        INC D_TMP
        BNE NOTE_WAIT_LOOP
        INC D_WAIT
        BNE NOTE_WAIT_LOOP
        INC D_WAIT+1
        JMP NOTE_WAIT_LOOP
NOTE_WAIT_DONE:
        RTS

; ---------------------------------------------------------------------------
; The keyboard: every key a person presses, with its code, for as long as the
; machine is left on. BREAK starts the tests again.
; ---------------------------------------------------------------------------
TEST_KEYS:
        LDA #<TXT_KEYS
        LDX #>TXT_KEYS
        JSR SAY
TEST_KEYS_LOOP:
        LDA #<TXT_KEY
        LDX #>TXT_KEY
        JSR SAY
        JSR K_CHRIN
        TAX
        JSR PRINT_HEX_BYTE
        LDA #' '
        JSR K_CHROUT
        TXA
        JSR K_CHROUT
        JSR END_OF_LINE
        JMP TEST_KEYS_LOOP

; ---------------------------------------------------------------------------
; Blocks: fill, copy and check, D_COUNT bytes at a time from (D_DST)
; ---------------------------------------------------------------------------
BLOCK_FILL:
        LDY #0
BLOCK_FILL_LOOP:
        LDA D_COUNT
        ORA D_COUNT+1
        BEQ BLOCK_FILL_DONE
        LDA D_PAT
        STA (D_DST),Y
        INC D_DST
        BNE BLOCK_FILL_STEP
        INC D_DST+1
BLOCK_FILL_STEP:
        JSR COUNT_DOWN
        JMP BLOCK_FILL_LOOP
BLOCK_FILL_DONE:
        RTS

BLOCK_COPY:
        LDY #0
BLOCK_COPY_LOOP:
        LDA D_COUNT
        ORA D_COUNT+1
        BEQ BLOCK_COPY_DONE
        LDA (D_SRC),Y
        STA (D_DST),Y
        INC D_SRC
        BNE BLOCK_COPY_SRC
        INC D_SRC+1
BLOCK_COPY_SRC:
        INC D_DST
        BNE BLOCK_COPY_STEP
        INC D_DST+1
BLOCK_COPY_STEP:
        JSR COUNT_DOWN
        JMP BLOCK_COPY_LOOP
BLOCK_COPY_DONE:
        RTS

; BLOCK_VERIFY: does every byte of the block hold the pattern? Carry set when
; one does not, and D_ADDR is it.
BLOCK_VERIFY:
        LDY #0
BLOCK_VERIFY_LOOP:
        LDA D_COUNT
        ORA D_COUNT+1
        BEQ BLOCK_VERIFY_GOOD
        LDA (D_DST),Y
        CMP D_PAT
        BNE BLOCK_VERIFY_BAD
        INC D_DST
        BNE BLOCK_VERIFY_STEP
        INC D_DST+1
BLOCK_VERIFY_STEP:
        JSR COUNT_DOWN
        JMP BLOCK_VERIFY_LOOP
BLOCK_VERIFY_GOOD:
        CLC
        RTS
BLOCK_VERIFY_BAD:
        LDA D_DST
        STA D_ADDR
        LDA D_DST+1
        STA D_ADDR+1
        SEC
        RTS

; COUNT_DOWN: D_COUNT one smaller.
COUNT_DOWN:
        LDA D_COUNT
        BNE COUNT_DOWN_LOW
        DEC D_COUNT+1
COUNT_DOWN_LOW:
        DEC D_COUNT
        RTS

; ---------------------------------------------------------------------------
; Small things
; ---------------------------------------------------------------------------
; SAY: A = low byte and X = high byte of a zero-terminated string.
SAY:
        STA ZP_STRPTR
        STX ZP_STRPTR+1
        JMP K_PRINT

END_OF_LINE:
        LDA #CC_RETURN
        JMP K_CHROUT

; PRINT_HEX_BYTE: A as two hex digits.
PRINT_HEX_BYTE:
        PHA
        LSR A
        LSR A
        LSR A
        LSR A
        JSR PRINT_NIBBLE
        PLA
        AND #$0F
PRINT_NIBBLE:
        CMP #10
        BCC PRINT_NIBBLE_DIGIT
        CLC
        ADC #'A'-10
        JMP K_CHROUT
PRINT_NIBBLE_DIGIT:
        CLC
        ADC #'0'
        JMP K_CHROUT

; PRINT_ADDRESS: the address at (D_SRC) as four hex digits.
PRINT_ADDRESS:
        LDA D_SRC+1
        JSR PRINT_HEX_BYTE
        LDA D_SRC
        JMP PRINT_HEX_BYTE

; ---------------------------------------------------------------------------
; Text
; ---------------------------------------------------------------------------
TXT_BANNER:
        .byte CC_CLS
        .text "*** KESTREL DIAGNOSTICS V1.0 ***"
        .byte 13, 13
        .byte 0

TXT_RAM:
        .text "RAM  $0800-$7FFF  "
        .byte 0
TXT_TUBE:
        .text "TUBE 40 X 25      "
        .byte 0
TXT_SOUND:
        .text "SOUND             "
        .byte 0
TXT_KEYS:
        .text "KEYBOARD          PRESS ANY KEY"
        .byte 13
        .text "                  BREAK TESTS AGAIN"
        .byte 13, 0
TXT_KEY:
        .text "KEY $"
        .byte 0

TXT_OK:
        .text "OK"
        .byte 0
TXT_FAIL:
        .text "FAIL AT "
        .byte 0
TXT_LISTEN:
        .text "LISTEN FOR THE NOTE"
        .byte 0
