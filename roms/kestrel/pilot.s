; SPDX-License-Identifier: MIT
;
; pilot.s -- Kestrel PILOT, the teaching language.
;
; PILOT is what a school bought when it wanted pupils writing programs rather
; than debugging them. A program is a list of numbered lines, each one a single
; statement; there are no variables to declare and no arithmetic to get wrong.
; A line types something, accepts an answer, or decides what to do about it, and
; a lesson is a sequence of those. It was the language of the classroom computer
; from the late seventies onwards, so a machine sold into schools had to have
; one.
;
; The statements, which are the whole language:
;
;       T: text         type the text, then a new line. $A prints answer A and
;                       $$ prints a dollar sign.
;       A: letter       accept an answer, into the variable for that letter.
;       M: text         the answer just accepted is this text; * matches
;                       anything. Sets the flag that Y: and N: test.
;       Y: label        go to the label when the answer matched.
;       N: label        go to the label when it did not.
;       J: label        go to the label.
;       E:              end the program.
;       R: text         a remark; the text is ignored.
;       :label          a label, which is where a jump goes.
;
; A line typed without a number is a statement, and is obeyed at once -- that is
; how a lesson is tried out before it is written down. A number typed on its own
; deletes that line. The commands are RUN, LIST and NEW. Everything is matched
; without regard to case.
;
; The program is kept as records in RAM, each one a line number, then the text,
; then a carriage return. A record whose line number is zero is the end of the
; program, so the whole of the program is one buffer that never needs a length:
; nothing here is copied on a RUN, and a jump is a scan of the same buffer.

        .include "kestrel.inc"

; ---------------------------------------------------------------------------
; Where PILOT keeps its things
; ---------------------------------------------------------------------------
PILOT_PGM   = $1000         ; the program, as records, up to RAM_TOP
PILOT_VARS  = $0900         ; 26 answers, sixteen bytes each
PILOT_STAGE = $0C00         ; a record on its way into the program
VAR_SIZE    = 16            ; an answer, with its terminator
VAR_COUNT   = 26
VAR_CHARS   = VAR_SIZE-1    ; how much of an answer is kept

; Zero page: the KERNAL's is $02-$1F and the ROM in the socket has $20 upwards,
; so everything here is a byte a person would only see if they looked.
P_PTR       = $20           ; $20/$21, the record being run or edited
P_TXT       = $22           ; $22/$23, the cursor in a statement's text
P_LINE      = $24           ; $24/$25, the line number being written
P_END       = $26           ; $26/$27, just past the last record
P_ARG       = $28           ; $28/$29, a number read off a line, or a record's own
P_SRC       = $2A           ; $2A/$2B, a text pointer, moving
P_DST       = $2C           ; $2C/$2D, the other one
P_COUNT     = $2E           ; $2E/$2F, how many bytes a move has left
P_LEN       = $30           ; $30/$31, the length of a record
P_VAR       = $32           ; $32/$33, the answer a statement is working with
P_TMP       = $34           ; $34/$35, a number on its way to the tube
P_TMP2      = $36
P_TMP3      = $37
P_CH        = $38
P_FLAG      = $39           ; did the last answer match?
P_ANSL      = $3A           ; the letter of the last answer accepted
P_NEWLEN    = $3C           ; $3C/$3D, the length of a record being written; it
                            ; is not P_LEN, which deleting the old line needs

        .org SOCKET

COLD_START_ENTRY:
        JMP PILOT_COLD
WARM_START_ENTRY:
        JMP PILOT_WARM

; ---------------------------------------------------------------------------
; Starting up
; ---------------------------------------------------------------------------
PILOT_COLD:
        LDA #<PILOT_BANNER
        STA ZP_STRPTR
        LDA #>PILOT_BANNER
        STA ZP_STRPTR+1
        JSR K_PRINT
        JSR VAR_CLEAR_ALL
        JSR CMD_NEW
        LDA #0
        STA P_FLAG
        STA P_ANSL
PILOT_WARM:
        JMP CMD_LOOP

; ---------------------------------------------------------------------------
; The command loop: a prompt, a line, and what the line asks for
; ---------------------------------------------------------------------------
CMD_LOOP:
        LDA #<TXT_PROMPT
        STA ZP_STRPTR
        LDA #>TXT_PROMPT
        STA ZP_STRPTR+1
        JSR K_PRINT
        JSR K_GETLINE
        LDA INBUF
        BEQ CMD_LOOP            ; an empty line is only an empty line
        CMP #'0'
        BCC CMD_KEYWORD
        CMP #'9'+1
        BCS CMD_KEYWORD
        JMP LINE_STORE

CMD_KEYWORD:
        LDA #<KW_RUN
        STA P_SRC
        LDA #>KW_RUN
        STA P_SRC+1
        JSR WORD_IS
        BCS CMD_RUN
        LDA #<KW_LIST
        STA P_SRC
        LDA #>KW_LIST
        STA P_SRC+1
        JSR WORD_IS
        BCS CMD_LIST
        LDA #<KW_NEW
        STA P_SRC
        LDA #>KW_NEW
        STA P_SRC+1
        JSR WORD_IS
        BCS CMD_NEW
        ; Not a command: it is a statement, and it is obeyed now.
        JMP CMD_IMMEDIATE

; WORD_IS: is INBUF this word? The word is at (P_SRC), in capitals, and matches
; when INBUF, read without regard to case, spells it and then ends or has a
; space. Carry set when it does.
WORD_IS:
        LDY #0
WORD_IS_LOOP:
        LDA (P_SRC),Y
        BEQ WORD_IS_END
        STA P_CH
        LDA INBUF,Y
        JSR UPCASE
        CMP P_CH
        BNE WORD_IS_NO
        INY
        JMP WORD_IS_LOOP
WORD_IS_END:
        LDA INBUF,Y
        BEQ WORD_IS_YES
        CMP #' '
        BEQ WORD_IS_YES
WORD_IS_NO:
        CLC
        RTS
WORD_IS_YES:
        SEC
        RTS

; -> A = the character, in capitals if it was a letter
UPCASE:
        CMP #'a'
        BCC UPCASE_DONE
        CMP #'z'+1
        BCS UPCASE_DONE
        SEC
        SBC #'a'-'A'
UPCASE_DONE:
        RTS

; ---------------------------------------------------------------------------
; The commands
; ---------------------------------------------------------------------------
CMD_RUN:
        LDA #<PILOT_PGM
        STA P_PTR
        LDA #>PILOT_PGM
        STA P_PTR+1
RUN_LOOP:
        JSR REC_LINE
        LDA P_ARG
        ORA P_ARG+1
        BEQ RUN_DONE            ; the zero record: the program has ended
        CLC                     ; the statement's text is just past the number
        LDA P_PTR
        ADC #2
        STA P_TXT
        LDA P_PTR+1
        ADC #0
        STA P_TXT+1
        JSR RUN_STMT
        CMP #0
        BEQ RUN_NEXT
        CMP #2
        BEQ RUN_LOOP            ; a jump has already put P_PTR where it goes
        JMP RUN_DONE
RUN_NEXT:
        JSR REC_NEXT
        JMP RUN_LOOP
RUN_DONE:
        LDA #CC_RETURN
        JSR K_CHROUT
        JMP CMD_LOOP

CMD_LIST:
        LDA #<PILOT_PGM
        STA P_PTR
        LDA #>PILOT_PGM
        STA P_PTR+1
LIST_LOOP:
        JSR REC_LINE
        LDA P_ARG
        ORA P_ARG+1
        BEQ CMD_LOOP
        JSR PRINT_RECORD
        JSR REC_NEXT
        JMP LIST_LOOP

CMD_NEW:
        LDA #<PILOT_PGM
        STA P_END
        LDA #>PILOT_PGM
        STA P_END+1
        LDY #0
        LDA #0
        STA (P_END),Y
        INY
        STA (P_END),Y
        INY
        LDA #CC_RETURN
        STA (P_END),Y
        RTS

CMD_IMMEDIATE:
        LDA #<INBUF
        STA P_TXT
        LDA #>INBUF
        STA P_TXT+1
        JSR RUN_STMT
        LDA #CC_RETURN
        JSR K_CHROUT
        JMP CMD_LOOP

; ---------------------------------------------------------------------------
; Writing a line: "10 T:HELLO" makes or replaces line 10, "10" deletes it
; ---------------------------------------------------------------------------
LINE_STORE:
        LDA #<INBUF
        STA P_TXT
        LDA #>INBUF
        STA P_TXT+1
        JSR SKIP_SPACES
        JSR READ_DECIMAL
        BCC LINE_STORE_BAD      ; no number at all
        LDA P_ARG
        ORA P_ARG+1
        BEQ LINE_STORE_BAD      ; line zero is the end marker, not a line
        LDA P_ARG
        STA P_LINE
        LDA P_ARG+1
        STA P_LINE+1
        JSR SKIP_SPACES
        LDY #0
        LDA (P_TXT),Y
        BEQ LINE_DELETE
        JMP LINE_STAGE

LINE_STORE_BAD:
        JMP SAY_WHAT

; The record is built in PILOT_STAGE: the line number, the text, a carriage
; return. Its length ends up in P_LEN.
LINE_STAGE:
        LDA #<PILOT_STAGE
        STA P_DST
        LDA #>PILOT_STAGE
        STA P_DST+1
        LDY #0
        LDA P_LINE
        STA (P_DST),Y
        INY
        LDA P_LINE+1
        STA (P_DST),Y
        CLC
        LDA P_DST
        ADC #2
        STA P_DST
        LDA P_DST+1
        ADC #0
        STA P_DST+1
        LDA #0
        STA P_LEN
        STA P_LEN+1
LINE_STAGE_COPY:
        LDY #0
        LDA (P_TXT),Y
        BEQ LINE_STAGE_CR
        CMP #CC_RETURN
        BEQ LINE_STAGE_CR
        STA (P_DST),Y
        JSR TXT_ADV
        INC P_DST
        BNE LINE_STAGE_STEP
        INC P_DST+1
LINE_STAGE_STEP:
        INC P_LEN
        BNE LINE_STAGE_COPY
        INC P_LEN+1
        JMP LINE_STAGE_COPY
LINE_STAGE_CR:
        LDA #CC_RETURN
        LDY #0
        STA (P_DST),Y
        CLC                     ; the two number bytes and the return
        LDA P_LEN
        ADC #3
        STA P_NEWLEN
        LDA P_LEN+1
        ADC #0
        STA P_NEWLEN+1
        JMP LINE_INSERT

; Where the record goes: after every record with a smaller number. A record with
; the same number is taken out first, which is what replaces a line.
LINE_INSERT:
        LDA #<PILOT_PGM
        STA P_PTR
        LDA #>PILOT_PGM
        STA P_PTR+1
LINE_INSERT_SCAN:
        JSR REC_LINE
        LDA P_ARG
        ORA P_ARG+1
        BEQ LINE_INSERT_HERE    ; the end of the program: it goes on the end
        JSR COMPARE_LINE
        BCC LINE_INSERT_NEXT
        LDA P_ARG
        CMP P_LINE
        BNE LINE_INSERT_HERE
        LDA P_ARG+1
        CMP P_LINE+1
        BNE LINE_INSERT_HERE
        JSR REC_DELETE          ; the old line, gone; P_PTR is the next one
        JMP LINE_INSERT_HERE
LINE_INSERT_NEXT:
        JSR REC_NEXT
        JMP LINE_INSERT_SCAN

LINE_INSERT_HERE:
        JSR REC_FIND_END
        JSR ROOM_FOR
        BCS LINE_NO_ROOM
        ; Make a hole the length of the record, and put the record in it.
        LDA P_PTR
        STA P_SRC
        LDA P_PTR+1
        STA P_SRC+1
        CLC
        LDA P_PTR
        ADC P_NEWLEN
        STA P_DST
        LDA P_PTR+1
        ADC P_NEWLEN+1
        STA P_DST+1
        SEC
        LDA P_END
        SBC P_PTR
        STA P_COUNT
        LDA P_END+1
        SBC P_PTR+1
        STA P_COUNT+1
        JSR MOVE_UP
        LDA #<PILOT_STAGE
        STA P_SRC
        LDA #>PILOT_STAGE
        STA P_SRC+1
        LDA P_PTR
        STA P_DST
        LDA P_PTR+1
        STA P_DST+1
        LDA P_NEWLEN
        STA P_COUNT
        LDA P_NEWLEN+1
        STA P_COUNT+1
        JSR MOVE_DOWN
        CLC
        LDA P_END
        ADC P_NEWLEN
        STA P_END
        LDA P_END+1
        ADC P_NEWLEN+1
        STA P_END+1
        JMP CMD_LOOP

LINE_NO_ROOM:
        LDA #<TXT_NO_ROOM
        STA ZP_STRPTR
        LDA #>TXT_NO_ROOM
        STA ZP_STRPTR+1
        JSR K_PRINT
        JMP CMD_LOOP

; A number on its own: take that line out, if it is there.
LINE_DELETE:
        LDA #<PILOT_PGM
        STA P_PTR
        LDA #>PILOT_PGM
        STA P_PTR+1
LINE_DELETE_SCAN:
        JSR REC_LINE
        LDA P_ARG
        ORA P_ARG+1
        BEQ CMD_LOOP            ; not there: nothing to do
        LDA P_ARG
        CMP P_LINE
        BNE LINE_DELETE_NEXT
        LDA P_ARG+1
        CMP P_LINE+1
        BNE LINE_DELETE_NEXT
        JSR REC_DELETE
        JMP CMD_LOOP
LINE_DELETE_NEXT:
        JSR REC_NEXT
        JMP LINE_DELETE_SCAN

; ---------------------------------------------------------------------------
; Records
; ---------------------------------------------------------------------------
; REC_LINE: the line number of the record at P_PTR, in P_ARG.
REC_LINE:
        LDY #0
        LDA (P_PTR),Y
        STA P_ARG
        INY
        LDA (P_PTR),Y
        STA P_ARG+1
        RTS

; REC_NEXT: P_PTR to the record after this one.
REC_NEXT:
        LDY #2
REC_NEXT_TEXT:
        LDA (P_PTR),Y
        CMP #CC_RETURN
        BEQ REC_NEXT_STEP
        INY
        BNE REC_NEXT_TEXT
REC_NEXT_STEP:
        TYA                     ; past the text and its return
        CLC
        ADC P_PTR
        STA P_PTR
        BCC REC_NEXT_CARRY
        INC P_PTR+1
REC_NEXT_CARRY:
        INC P_PTR
        BNE REC_NEXT_DONE
        INC P_PTR+1
REC_NEXT_DONE:
        RTS

; REC_LEN: the length of the record at P_PTR, in P_LEN.
REC_LEN:
        LDY #2
REC_LEN_TEXT:
        LDA (P_PTR),Y
        CMP #CC_RETURN
        BEQ REC_LEN_DONE
        INY
        BNE REC_LEN_TEXT
REC_LEN_DONE:
        INY
        TYA
        STA P_LEN
        LDA #0
        STA P_LEN+1
        RTS

; REC_FIND_END: P_END, the address just past the zero record that ends the
; program. The end record is part of the program -- it is what a move has to
; carry up or down with everything else -- so P_END is past it, not on it.
REC_FIND_END:
        LDA #<PILOT_PGM
        STA P_SRC
        LDA #>PILOT_PGM
        STA P_SRC+1
REC_FIND_END_LOOP:
        LDY #0
        LDA (P_SRC),Y
        INY
        ORA (P_SRC),Y
        BEQ REC_FIND_END_DONE
        LDY #2
REC_FIND_END_TEXT:
        LDA (P_SRC),Y
        CMP #CC_RETURN
        BEQ REC_FIND_END_STEP
        INY
        BNE REC_FIND_END_TEXT
REC_FIND_END_STEP:
        TYA                     ; past the text, then past the return
        CLC
        ADC P_SRC
        STA P_SRC
        BCC REC_FIND_END_ADVANCE
        INC P_SRC+1
REC_FIND_END_ADVANCE:
        INC P_SRC
        BNE REC_FIND_END_LOOP
        INC P_SRC+1
        JMP REC_FIND_END_LOOP
REC_FIND_END_DONE:
        CLC                     ; past the end record: two bytes and its return
        LDA P_SRC
        ADC #3
        STA P_END
        LDA P_SRC+1
        ADC #0
        STA P_END+1
        RTS

; REC_DELETE: take the record at P_PTR out of the program; P_PTR is left on the
; record that followed it.
REC_DELETE:
        JSR REC_LEN
        JSR REC_FIND_END
        CLC
        LDA P_PTR
        ADC P_LEN
        STA P_SRC
        LDA P_PTR+1
        ADC P_LEN+1
        STA P_SRC+1
        LDA P_PTR
        STA P_DST
        LDA P_PTR+1
        STA P_DST+1
        SEC
        LDA P_END
        SBC P_SRC
        STA P_COUNT
        LDA P_END+1
        SBC P_SRC+1
        STA P_COUNT+1
        JSR MOVE_DOWN
        SEC
        LDA P_END
        SBC P_LEN
        STA P_END
        LDA P_END+1
        SBC P_LEN+1
        STA P_END+1
        RTS

; COMPARE_LINE: carry set when the record's number (P_ARG) is the one being
; written (P_LINE) or above it.
COMPARE_LINE:
        LDA P_ARG+1
        CMP P_LINE+1
        BCC COMPARE_LINE_BELOW
        BNE COMPARE_LINE_ABOVE
        LDA P_ARG
        CMP P_LINE
        BCC COMPARE_LINE_BELOW
COMPARE_LINE_ABOVE:
        SEC
        RTS
COMPARE_LINE_BELOW:
        CLC
        RTS

; ROOM_FOR: is there space for the record being written? Carry set when there
; is not.
ROOM_FOR:
        JSR REC_FIND_END
        CLC
        LDA P_END
        ADC P_NEWLEN
        LDA P_END+1
        ADC P_NEWLEN+1
        CMP #>(RAM_TOP+1)       ; $8000: the first address that is not RAM
        BCS ROOM_FOR_NO
        CLC
        RTS
ROOM_FOR_NO:
        SEC
        RTS

; MOVE_DOWN: P_COUNT bytes from P_SRC to P_DST, forwards; for a destination
; below the source.
MOVE_DOWN:
        LDY #0
MOVE_DOWN_LOOP:
        LDA P_COUNT
        ORA P_COUNT+1
        BEQ MOVE_DOWN_DONE
        LDA (P_SRC),Y
        STA (P_DST),Y
        INC P_SRC
        BNE MOVE_DOWN_SRC
        INC P_SRC+1
MOVE_DOWN_SRC:
        INC P_DST
        BNE MOVE_DOWN_DST
        INC P_DST+1
MOVE_DOWN_DST:
        LDA P_COUNT
        BNE MOVE_DOWN_COUNT
        DEC P_COUNT+1
MOVE_DOWN_COUNT:
        DEC P_COUNT
        JMP MOVE_DOWN_LOOP
MOVE_DOWN_DONE:
        RTS

; MOVE_UP: P_COUNT bytes from P_SRC to P_DST, backwards; for a destination
; above the source, where a forward copy would eat what it was copying.
MOVE_UP:
        CLC
        LDA P_SRC
        ADC P_COUNT
        STA P_SRC
        LDA P_SRC+1
        ADC P_COUNT+1
        STA P_SRC+1
        CLC
        LDA P_DST
        ADC P_COUNT
        STA P_DST
        LDA P_DST+1
        ADC P_COUNT+1
        STA P_DST+1
MOVE_UP_LOOP:
        LDA P_COUNT
        ORA P_COUNT+1
        BEQ MOVE_UP_DONE
        LDA P_SRC
        BNE MOVE_UP_SRC
        DEC P_SRC+1
MOVE_UP_SRC:
        DEC P_SRC
        LDA P_DST
        BNE MOVE_UP_DST
        DEC P_DST+1
MOVE_UP_DST:
        DEC P_DST
        LDY #0
        LDA (P_SRC),Y
        STA (P_DST),Y
        LDA P_COUNT
        BNE MOVE_UP_COUNT
        DEC P_COUNT+1
MOVE_UP_COUNT:
        DEC P_COUNT
        JMP MOVE_UP_LOOP
MOVE_UP_DONE:
        RTS

; ---------------------------------------------------------------------------
; The interpreter
; ---------------------------------------------------------------------------
; RUN_STMT: run the statement at P_TXT. A = 0 to carry on with the next record,
; 1 to stop the program, 2 when a jump has already moved P_PTR.
RUN_STMT:
        JSR SKIP_SPACES
        LDY #0
        LDA (P_TXT),Y
        BEQ STMT_NEXT
        CMP #CC_RETURN
        BEQ STMT_NEXT
        JSR UPCASE
        CMP #'T'
        BEQ STMT_TYPE
        CMP #'A'
        BEQ STMT_ACCEPT
        CMP #'M'
        BEQ STMT_MATCH
        CMP #'J'
        BEQ STMT_JUMP
        CMP #'Y'
        BEQ STMT_YES
        CMP #'N'
        BEQ STMT_NO
        CMP #'E'
        BEQ STMT_END
        CMP #'R'
        BEQ STMT_NEXT
        CMP #':'
        BEQ STMT_NEXT           ; a label does nothing when it is run into
        JMP SAY_WHAT_STOP

STMT_NEXT:
        LDA #0
        RTS
STMT_STOP:
        LDA #1
        RTS
STMT_JUMPED:
        LDA #2
        RTS

; STMT_HEAD: past the statement's letter and its colon, and any spaces.
STMT_HEAD:
        JSR TXT_ADV
        JSR TXT_ADV
        JMP SKIP_SPACES

; T: -- type the text, one character at a time, then a new line.
STMT_TYPE:
        JSR STMT_HEAD
STMT_TYPE_LOOP:
        LDY #0
        LDA (P_TXT),Y
        BEQ STMT_TYPE_EOL
        CMP #CC_RETURN
        BEQ STMT_TYPE_EOL
        CMP #'$'
        BEQ STMT_TYPE_DOLLAR
        JSR K_CHROUT
        JSR TXT_ADV
        JMP STMT_TYPE_LOOP
STMT_TYPE_DOLLAR:
        JSR TXT_ADV
        LDY #0
        LDA (P_TXT),Y
        CMP #'$'
        BNE STMT_TYPE_VAR
        JSR K_CHROUT            ; $$ is one dollar sign
        JSR TXT_ADV
        JMP STMT_TYPE_LOOP
STMT_TYPE_VAR:
        JSR UPCASE
        JSR VAR_POINT
        BCC STMT_TYPE_NOT_VAR
        JSR VAR_PRINT
        JSR TXT_ADV
        JMP STMT_TYPE_LOOP
STMT_TYPE_NOT_VAR:
        LDY #0
        LDA (P_TXT),Y
        JSR K_CHROUT
        JSR TXT_ADV
        JMP STMT_TYPE_LOOP
STMT_TYPE_EOL:
        LDA #CC_RETURN
        JSR K_CHROUT
        JMP STMT_NEXT

; A: -- accept an answer into the named variable.
STMT_ACCEPT:
        JSR STMT_HEAD
        LDY #0
        LDA (P_TXT),Y
        JSR UPCASE
        STA P_ANSL
        JSR VAR_POINT
        BCC SAY_WHAT_STOP       ; A: wants a letter
        JSR VAR_CLEAR
        JSR K_GETLINE
        JMP VAR_KEEP

; VAR_KEEP: the line just typed, into the variable at P_VAR -- up to VAR_CHARS
; characters, with the spaces on the end taken off -- and then the next record.
VAR_KEEP:
        LDX #0
        LDY #0
VAR_KEEP_COPY:
        LDA INBUF,X
        BEQ VAR_KEEP_TRIM
        CMP #CC_RETURN
        BEQ VAR_KEEP_TRIM
        CPX #VAR_CHARS
        BCS VAR_KEEP_TRIM
        STA (P_VAR),Y
        INX
        INY
        JMP VAR_KEEP_COPY
VAR_KEEP_TRIM:
        CPX #0
        BEQ VAR_KEEP_STORE
        DEX
        TXA
        TAY
        LDA (P_VAR),Y
        CMP #' '
        BEQ VAR_KEEP_TRIM
        INX
VAR_KEEP_STORE:
        TXA
        TAY
        LDA #0
        STA (P_VAR),Y
        JMP STMT_NEXT

; M: -- does the answer just accepted match?
STMT_MATCH:
        JSR STMT_HEAD
        LDY #0
        LDA (P_TXT),Y
        CMP #'*'
        BNE STMT_MATCH_TEXT
        LDA #1
        STA P_FLAG
        JMP STMT_NEXT
STMT_MATCH_TEXT:
        LDA P_ANSL
        BNE STMT_MATCH_VAR
        LDA #0                  ; nothing has been accepted, so nothing matches
        STA P_FLAG
        JMP STMT_NEXT
STMT_MATCH_VAR:
        LDA P_ANSL
        JSR VAR_POINT
        BCC STMT_MATCH_NO
        LDY #0
STMT_MATCH_LOOP:
        LDA (P_VAR),Y
        BNE STMT_MATCH_HAVE
        ; The answer has ended: the text must have nothing left but spaces.
STMT_MATCH_TAIL:
        LDA (P_TXT),Y
        CMP #' '
        BEQ STMT_MATCH_TAIL_NEXT
        CMP #CC_RETURN
        BEQ STMT_MATCH_YES
        CMP #0
        BEQ STMT_MATCH_YES
        JMP STMT_MATCH_NO
STMT_MATCH_TAIL_NEXT:
        INY
        JMP STMT_MATCH_TAIL
STMT_MATCH_HAVE:
        JSR UPCASE
        STA P_CH
        LDA (P_TXT),Y
        JSR UPCASE
        CMP P_CH
        BNE STMT_MATCH_NO
        INY
        JMP STMT_MATCH_LOOP
STMT_MATCH_YES:
        LDA #1
        STA P_FLAG
        JMP STMT_NEXT
STMT_MATCH_NO:
        LDA #0
        STA P_FLAG
        JMP STMT_NEXT

; Y:, N: and J: -- go to a label.
STMT_YES:
        LDA P_FLAG
        BEQ STMT_NEXT
        JMP STMT_JUMP
STMT_NO:
        LDA P_FLAG
        BNE STMT_NEXT
STMT_JUMP:
        JSR STMT_HEAD
        JSR LABEL_FIND
        BCC STMT_NO_LABEL
        JMP STMT_JUMPED

STMT_NO_LABEL:
        LDA #<TXT_NO_LABEL
        STA ZP_STRPTR
        LDA #>TXT_NO_LABEL
        STA ZP_STRPTR+1
        JSR K_PRINT
        JMP STMT_STOP

STMT_END:
        JMP STMT_STOP

SAY_WHAT_STOP:
        LDA #<TXT_WHAT
        STA ZP_STRPTR
        LDA #>TXT_WHAT
        STA ZP_STRPTR+1
        JSR K_PRINT
        JMP STMT_STOP
SAY_WHAT:
        LDA #<TXT_WHAT
        STA ZP_STRPTR
        LDA #>TXT_WHAT
        STA ZP_STRPTR+1
        JSR K_PRINT
        JMP CMD_LOOP

; LABEL_FIND: look through the program for the record whose text is ":" and
; then the label at P_TXT. Carry set, and P_PTR on that record, when it is
; there. P_DST walks the label as it was written; the stored one is walked with
; Y, because a label is compared a character at a time.
LABEL_FIND:
        LDA #<PILOT_PGM
        STA P_SRC
        LDA #>PILOT_PGM
        STA P_SRC+1
LABEL_FIND_REC:
        LDY #0
        LDA (P_SRC),Y
        INY
        ORA (P_SRC),Y
        BEQ LABEL_FIND_NO       ; the end of the program
        LDA P_TXT
        STA P_DST
        LDA P_TXT+1
        STA P_DST+1
        LDY #2
        LDA (P_SRC),Y
        CMP #':'
        BNE LABEL_FIND_NEXT
        INY                     ; the first character of the stored label
LABEL_FIND_CH:
        LDA (P_SRC),Y
        CMP #CC_RETURN
        BEQ LABEL_FIND_TAIL
        CMP #0
        BEQ LABEL_FIND_TAIL
        JSR UPCASE
        STA P_CH
        STY P_TMP3
        LDY #0
        LDA (P_DST),Y
        JSR UPCASE
        CMP P_CH
        BNE LABEL_FIND_NEXT
        INC P_DST
        BNE LABEL_FIND_CH_STEP
        INC P_DST+1
LABEL_FIND_CH_STEP:
        LDY P_TMP3
        INY
        JMP LABEL_FIND_CH
LABEL_FIND_TAIL:
        LDY #0
LABEL_FIND_TAIL_LOOP:
        LDA (P_DST),Y
        CMP #' '
        BEQ LABEL_FIND_TAIL_NEXT
        CMP #CC_RETURN
        BEQ LABEL_FIND_YES
        CMP #0
        BEQ LABEL_FIND_YES
        JMP LABEL_FIND_NEXT
LABEL_FIND_TAIL_NEXT:
        INC P_DST
        BNE LABEL_FIND_TAIL_LOOP
        INC P_DST+1
        JMP LABEL_FIND_TAIL_LOOP
LABEL_FIND_NEXT:
        LDY #2
LABEL_FIND_SKIP:
        LDA (P_SRC),Y
        CMP #CC_RETURN
        BEQ LABEL_FIND_STEP
        INY
        BNE LABEL_FIND_SKIP
LABEL_FIND_STEP:
        TYA
        CLC
        ADC P_SRC
        STA P_SRC
        BCC LABEL_FIND_STEP2
        INC P_SRC+1
LABEL_FIND_STEP2:
        INC P_SRC
        BNE LABEL_FIND_REC
        INC P_SRC+1
        JMP LABEL_FIND_REC
LABEL_FIND_YES:
        LDA P_SRC
        STA P_PTR
        LDA P_SRC+1
        STA P_PTR+1
        SEC
        RTS
LABEL_FIND_NO:
        CLC
        RTS

; ---------------------------------------------------------------------------
; Variables: one answer per letter, kept in PILOT_VARS
; ---------------------------------------------------------------------------
; VAR_POINT: A = a letter -> P_VAR, the answer it names; carry clear when A is
; not one of the letters.
VAR_POINT:
        CMP #'A'
        BCC VAR_POINT_NO
        CMP #'Z'+1
        BCS VAR_POINT_NO
        SEC
        SBC #'A'
        ASL A
        ASL A
        ASL A
        ASL A
        CLC
        ADC #<PILOT_VARS
        STA P_VAR
        LDA #>PILOT_VARS
        ADC #0
        STA P_VAR+1
        SEC
        RTS
VAR_POINT_NO:
        CLC
        RTS

VAR_CLEAR:
        LDY #0
        LDA #0
VAR_CLEAR_LOOP:
        STA (P_VAR),Y
        INY
        CPY #VAR_SIZE
        BNE VAR_CLEAR_LOOP
        RTS

VAR_CLEAR_ALL:
        LDA #<PILOT_VARS
        STA P_VAR
        LDA #>PILOT_VARS
        STA P_VAR+1
        LDA #<(VAR_COUNT*VAR_SIZE)
        STA P_COUNT
        LDA #>(VAR_COUNT*VAR_SIZE)
        STA P_COUNT+1
        LDY #0
VAR_CLEAR_ALL_LOOP:
        LDA P_COUNT
        ORA P_COUNT+1
        BEQ VAR_CLEAR_ALL_DONE
        LDA #0
        STA (P_VAR),Y
        INC P_VAR
        BNE VAR_CLEAR_ALL_STEP
        INC P_VAR+1
VAR_CLEAR_ALL_STEP:
        LDA P_COUNT
        BNE VAR_CLEAR_ALL_COUNT
        DEC P_COUNT+1
VAR_CLEAR_ALL_COUNT:
        DEC P_COUNT
        JMP VAR_CLEAR_ALL_LOOP
VAR_CLEAR_ALL_DONE:
        RTS

VAR_PRINT:
        LDY #0
VAR_PRINT_LOOP:
        LDA (P_VAR),Y
        BEQ VAR_PRINT_DONE
        JSR K_CHROUT
        INY
        JMP VAR_PRINT_LOOP
VAR_PRINT_DONE:
        RTS

; ---------------------------------------------------------------------------
; Reading a line
; ---------------------------------------------------------------------------
; TXT_ADV: P_TXT on, one character.
TXT_ADV:
        INC P_TXT
        BNE TXT_ADV_DONE
        INC P_TXT+1
TXT_ADV_DONE:
        RTS

; SKIP_SPACES: P_TXT past any spaces.
SKIP_SPACES:
        LDY #0
        LDA (P_TXT),Y
        CMP #' '
        BNE SKIP_SPACES_DONE
        JSR TXT_ADV
        JMP SKIP_SPACES
SKIP_SPACES_DONE:
        RTS

; READ_DECIMAL: the number at P_TXT into P_ARG, P_TXT past it. Carry set when
; there was one.
READ_DECIMAL:
        LDA #0
        STA P_ARG
        STA P_ARG+1
        STA P_CH               ; how many digits there were
READ_DECIMAL_LOOP:
        LDY #0
        LDA (P_TXT),Y
        SEC
        SBC #'0'
        BCC READ_DECIMAL_DONE
        CMP #10
        BCS READ_DECIMAL_DONE
        STA P_TMP3             ; the digit
        ; P_ARG = P_ARG * 10 + digit
        LDA P_ARG
        STA P_TMP
        LDA P_ARG+1
        STA P_TMP+1
        ASL P_ARG
        ROL P_ARG+1
        ASL P_ARG
        ROL P_ARG+1
        CLC
        LDA P_ARG
        ADC P_TMP
        STA P_ARG
        LDA P_ARG+1
        ADC P_TMP+1
        STA P_ARG+1
        ASL P_ARG
        ROL P_ARG+1
        CLC
        LDA P_ARG
        ADC P_TMP3
        STA P_ARG
        LDA P_ARG+1
        ADC #0
        STA P_ARG+1
        INC P_CH
        JSR TXT_ADV
        JMP READ_DECIMAL_LOOP
READ_DECIMAL_DONE:
        LDA P_CH
        BEQ READ_DECIMAL_NONE
        SEC
        RTS
READ_DECIMAL_NONE:
        CLC
        RTS

; ---------------------------------------------------------------------------
; Printing
; ---------------------------------------------------------------------------
; PRINT_RECORD: the record at P_PTR as a person would type it.
PRINT_RECORD:
        LDA P_ARG
        STA P_TMP
        LDA P_ARG+1
        STA P_TMP+1
        JSR PRINT_DECIMAL
        LDA #' '
        JSR K_CHROUT
        LDY #2
PRINT_RECORD_TEXT:
        LDA (P_PTR),Y
        CMP #CC_RETURN
        BEQ PRINT_RECORD_DONE
        JSR K_CHROUT
        INY
        JMP PRINT_RECORD_TEXT
PRINT_RECORD_DONE:
        LDA #CC_RETURN
        JSR K_CHROUT
        RTS

; PRINT_DECIMAL: P_TMP/P_TMP+1 in decimal, without leading zeroes.
PRINT_DECIMAL:
        LDX #0
PRINT_DECIMAL_DIGIT:
        JSR DIVIDE_TEN
        PHA
        INX
        LDA P_TMP
        ORA P_TMP+1
        BNE PRINT_DECIMAL_DIGIT
PRINT_DECIMAL_OUT:
        PLA
        CLC
        ADC #'0'
        JSR K_CHROUT
        DEX
        BNE PRINT_DECIMAL_OUT
        RTS

; DIVIDE_TEN: P_TMP/P_TMP+1 divided by ten, quotient back there and the
; remainder in A. X is kept, because the digits above are counted in it -- and
; keeping it means the remainder has to be put aside while the stack gives X
; back, since PLA loads A.
DIVIDE_TEN:
        TXA
        PHA
        LDA #0
        STA P_TMP3
        LDX #16
DIVIDE_TEN_LOOP:
        ASL P_TMP
        ROL P_TMP+1
        ROL P_TMP3
        LDA P_TMP3
        SEC
        SBC #10
        BCC DIVIDE_TEN_NEXT
        STA P_TMP3
        INC P_TMP
DIVIDE_TEN_NEXT:
        DEX
        BNE DIVIDE_TEN_LOOP
        LDA P_TMP3
        STA P_TMP2
        PLA
        TAX
        LDA P_TMP2
        RTS

; ---------------------------------------------------------------------------
; Text
; ---------------------------------------------------------------------------
PILOT_BANNER:
        .byte CC_CLS
        .text "*** KESTREL PILOT V1.0 ***"
        .byte 13
        .text "A number writes a line, RUN runs it, LIST shows it."
        .byte 13
        .text "T: A: M: Y: N: J: E: R: :LABEL"
        .byte 13, 13, 0

TXT_PROMPT:
        .text ">"
        .byte 0

KW_RUN:
        .text "RUN"
        .byte 0
KW_LIST:
        .text "LIST"
        .byte 0
KW_NEW:
        .text "NEW"
        .byte 0

TXT_WHAT:
        .text "?"
        .byte 13, 0

TXT_NO_ROOM:
        .text "out of memory"
        .byte 13, 0

TXT_NO_LABEL:
        .text "no such label"
        .byte 13, 0
