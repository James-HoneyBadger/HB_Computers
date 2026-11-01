; SPDX-License-Identifier: MIT
;
; ary.s -- arrays. An array is a descriptor in the same heap as the strings,
; chained from the head at B_FRETOP:
;
;   +0/+1  link to the next descriptor, 0 at the end of the chain
;   +2/+3  the name
;   +4     how many dimensions, with bit 7 set for an array of strings
;   +5/+6  how many elements
;   +7..   the size of each dimension, two bytes each
;   then   the elements, five bytes each, the last subscript moving fastest
;
; Nothing is ever released, so an array keeps its room for the life of the
; program, as it did on a machine of this size. Dimensions are limited to 254,
; because the sizes are multiplied together eight bits at a time.

; ---------------------------------------------------------------------------
; Odds and ends
; ---------------------------------------------------------------------------

; ARY_MUL8: B_ARYTMP = B_ARYTMP * B_ARYMUL, the low sixteen bits, for a
; multiplier that fits in one byte.
ARY_MUL8:
        LDA #0
        STA B_ARYSUM
        STA B_ARYSUM+1
        LDA #8
        STA B_ARYCNT
ARY_MUL8_LOOP:
        LSR B_ARYMUL
        BCC ARY_MUL8_SKIP
        CLC
        LDA B_ARYSUM
        ADC B_ARYTMP
        STA B_ARYSUM
        LDA B_ARYSUM+1
        ADC B_ARYTMP+1
        STA B_ARYSUM+1
ARY_MUL8_SKIP:
        ASL B_ARYTMP
        ROL B_ARYTMP+1
        DEC B_ARYCNT
        BNE ARY_MUL8_LOOP
        LDA B_ARYSUM
        STA B_ARYTMP
        LDA B_ARYSUM+1
        STA B_ARYTMP+1
        RTS

; ARY_DATA: B_ARYPTR walks to the first element of the descriptor it points at,
; which is B_ARYPTR plus the header.
ARY_DATA:
        LDY #4
        LDA (B_ARYPTR),Y
        AND #$7F
        ASL A
        CLC
        ADC #7
        BCC ARY_DATA_ADD
        INY                     ; carry into the high byte below
        LDA #0
        STA B_ARYMUL
ARY_DATA_ADD:
        STA B_ARYMUL
        CLC
        LDA B_ARYPTR
        ADC B_ARYMUL
        STA B_ARYPTR
        LDA B_ARYPTR+1
        ADC #0
        STA B_ARYPTR+1
        RTS

; ---------------------------------------------------------------------------
; DIM
; ---------------------------------------------------------------------------

DO_DIM:
        JSR PARSENAME
        BCS ERR_SYNTAX
        LDA B_TEMP
        STA B_VNAME1
        LDA B_TEMP2
        STA B_VNAME2
        LDA B_TEMP3
        STA B_ARYTYPE           ; the type, kept for the descriptor
DIM_SIZES:
        JSR ARY_SIZES           ; sizes into B_DIMS, count and total alongside
        LDA B_VNAME1            ; the sizes were expressions, so the name comes
        STA B_TEMP              ; back out of the pair kept for it
        LDA B_VNAME2
        STA B_TEMP2
        LDA B_DIMTOT
        STA B_TMP
        LDA B_DIMTOT+1
        STA B_TMP+1
        JSR ARY_ALLOC
        BCS ERR_OUT_OF_MEMORY
        ; the descriptor is now at B_DIMPTR: name, count, total, sizes
        LDA B_DIMPTR
        STA B_TMPP
        LDA B_DIMPTR+1
        STA B_TMPP+1
        LDY #4
        LDA B_DIMN
        LDX B_ARYTYPE
        CPX #T_STR
        BNE DIM_STORE_NDIMS
        ORA #$80
DIM_STORE_NDIMS:
        STA (B_TMPP),Y
        INY
        LDA B_DIMTOT
        STA (B_TMPP),Y
        INY
        LDA B_DIMTOT+1
        STA (B_TMPP),Y
        LDA #0
        STA B_ARYIDX
DIM_COPY_DIMS:
        LDA B_ARYIDX
        CMP B_DIMN
        BEQ DIM_LINK
        ASL A
        TAX
        LDA B_DIMS,X
        STA B_ARYSUB
        LDA B_DIMS+1,X
        STA B_ARYSUB+1
        LDA B_ARYIDX
        ASL A
        CLC
        ADC #7
        TAY
        LDA B_ARYSUB
        STA (B_TMPP),Y
        INY
        LDA B_ARYSUB+1
        STA (B_TMPP),Y
        INC B_ARYIDX
        JMP DIM_COPY_DIMS
DIM_LINK:
        JSR ARY_LINK
        JSR SKIPSP
        JSR CURCHAR
        CMP #','
        BNE DIM_DONE
        JSR ADV
        JMP DO_DIM
DIM_DONE:
        CLC
        RTS

; ARY_SIZES: reads "(n[,n...])" and leaves the sizes in B_DIMS, how many in
; B_DIMN and how many elements they come to in B_DIMTOT.
ARY_SIZES:
        JSR STRARG_OPEN
        LDA #0
        STA B_DIMN
        LDA #1
        STA B_DIMTOT
        LDA #0
        STA B_DIMTOT+1
ARY_SIZES_LOOP:
        JSR EXPR
        JSR STRNONE
        JSR FTOI
        BCS ERR_BAD_SUBSCRIPT
        STA B_ARYSUB
        STY B_ARYSUB+1
        LDA B_ARYSUB+1
        BNE ERR_BAD_SUBSCRIPT   ; too large for this machine
        LDA B_DIMN
        CMP #4
        BCS ERR_BAD_SUBSCRIPT   ; four dimensions is plenty
        ; the size goes in the table
        ASL A
        TAX
        LDA B_ARYSUB
        STA B_DIMS,X
        LDA #0
        STA B_DIMS+1,X
        ; and the total multiplies by one more than it
        LDA B_ARYSUB
        CLC
        ADC #1
        STA B_ARYMUL
        LDA B_DIMTOT
        STA B_ARYTMP
        LDA B_DIMTOT+1
        STA B_ARYTMP+1
        JSR ARY_MUL8
        LDA B_ARYTMP
        STA B_DIMTOT
        LDA B_ARYTMP+1
        STA B_DIMTOT+1
        LDA B_DIMTOT+1
        CMP #$80
        BCS ERR_OUT_OF_MEMORY
        INC B_DIMN
        JSR SKIPSP
        JSR CURCHAR
        CMP #','
        BNE ARY_SIZES_END
        JSR ADV
        JMP ARY_SIZES_LOOP
ARY_SIZES_END:
        JMP STRARG_CLOSE

; ARY_ALLOC:
; ARY_ALLOC: room for B_DIMTOT elements described by B_DIMN dimensions. The
; block is cleared and B_DIMPTR holds its address; carry set when the heap is
; full.
ARY_ALLOC:
        ; five bytes an element
        LDA B_DIMTOT
        STA B_ARYTMP
        LDA B_DIMTOT+1
        STA B_ARYTMP+1
        LDA #5
        STA B_ARYMUL
        JSR ARY_MUL8            ; B_ARYTMP = elements * 5
        ; plus seven bytes and two for each dimension
        LDA B_DIMN
        ASL A
        CLC
        ADC #7
        STA B_ARYMUL
        LDA B_ARYTMP
        CLC
        ADC B_ARYMUL
        STA B_ARYTMP
        LDA B_ARYTMP+1
        ADC #0
        STA B_ARYTMP+1
        LDA B_ARYTMP
        STA B_TMP               ; the size of the block
        LDA B_ARYTMP+1
        STA B_TMP+1
        JSR STRALLOC
        BCS ARY_ALLOC_FULL
        LDA B_STREND
        STA B_DIMPTR
        LDA B_STREND+1
        STA B_DIMPTR+1
        ; the whole block comes up zero, name and all
        LDA B_DIMPTR
        STA B_ARYPTR
        LDA B_DIMPTR+1
        STA B_ARYPTR+1
        LDA B_ARYTMP
        STA B_ARYSUB
        LDA B_ARYTMP+1
        STA B_ARYSUB+1
ARY_ALLOC_ZERO:
        LDA B_ARYSUB
        ORA B_ARYSUB+1
        BEQ ARY_ALLOC_ZEROED
        LDY #0
        LDA #0
        STA (B_ARYPTR),Y
        INC B_ARYPTR
        BNE ARY_ALLOC_TICK
        INC B_ARYPTR+1
ARY_ALLOC_TICK:
        LDA B_ARYSUB
        BNE ARY_ALLOC_DEC
        DEC B_ARYSUB+1
ARY_ALLOC_DEC:
        DEC B_ARYSUB
        JMP ARY_ALLOC_ZERO
ARY_ALLOC_ZEROED:
        ; the name goes in once the block has been cleared under it
        LDY #2
        LDA B_TEMP
        STA (B_DIMPTR),Y
        INY
        LDA B_TEMP2
        STA (B_DIMPTR),Y
        CLC
        RTS
ARY_ALLOC_FULL:
        SEC
        RTS

; ARY_LINK: the descriptor at B_DIMPTR joins the head of the chain, unless one
; of that name is already there, which is a redimension.
ARY_LINK:
        LDA B_FRETOP
        STA B_ARYPTR
        LDA B_FRETOP+1
        STA B_ARYPTR+1
ARY_LINK_LOOK:
        LDA B_ARYPTR
        ORA B_ARYPTR+1
        BEQ ARY_LINK_NEW
        LDY #2
        LDA (B_ARYPTR),Y
        CMP B_TEMP
        BNE ARY_LINK_NEXT
        INY
        LDA (B_ARYPTR),Y
        CMP B_TEMP2
        BNE ARY_LINK_NEXT
        LDY #4
        LDA (B_ARYPTR),Y
        AND #$80
        CMP B_ARYTYPE           ; A and A$ are different arrays
        BEQ ARY_LINK_REDIM
ARY_LINK_NEXT:
        LDY #0
        LDA (B_ARYPTR),Y
        PHA
        INY
        LDA (B_ARYPTR),Y
        STA B_ARYPTR+1
        PLA
        STA B_ARYPTR
        JMP ARY_LINK_LOOK
ARY_LINK_REDIM:
        JMP ERR_REDIMENSIONED
ARY_LINK_NEW:
        LDA B_FRETOP
        LDY #0
        STA (B_DIMPTR),Y
        LDA B_FRETOP+1
        INY
        STA (B_DIMPTR),Y
        LDA B_DIMPTR
        STA B_FRETOP
        LDA B_DIMPTR+1
        STA B_FRETOP+1
        RTS

; ---------------------------------------------------------------------------
; Using an element
; ---------------------------------------------------------------------------

; ARY_LOOKUP: the array named in B_TEMP/B_TEMP2. A name that has never been
; dimensioned becomes a one-dimensional array of eleven, as it always did.
; -> B_ARYPTR.
ARY_LOOKUP:
        LDA #0
        LDX B_TEMP3
        CPX #T_STR
        BNE ARY_LOOKUP_TYPE
        LDA #$80
ARY_LOOKUP_TYPE:
        STA B_ARYTYPE
        LDA B_FRETOP
        STA B_ARYPTR
        LDA B_FRETOP+1
        STA B_ARYPTR+1
ARY_LOOKUP_LOOP:
        LDA B_ARYPTR
        ORA B_ARYPTR+1
        BEQ ARY_LOOKUP_NEW
        LDY #2
        LDA (B_ARYPTR),Y
        CMP B_TEMP
        BNE ARY_LOOKUP_NEXT
        INY
        LDA (B_ARYPTR),Y
        CMP B_TEMP2
        BNE ARY_LOOKUP_NEXT
        ; the type has to match too: A and A$ are different arrays
        LDY #4
        LDA (B_ARYPTR),Y
        AND #$80
        CMP B_ARYTYPE
        BEQ ARY_LOOKUP_DONE
ARY_LOOKUP_NEXT:
        LDY #0
        LDA (B_ARYPTR),Y
        PHA
        INY
        LDA (B_ARYPTR),Y
        STA B_ARYPTR+1
        PLA
        STA B_ARYPTR
        JMP ARY_LOOKUP_LOOP
ARY_LOOKUP_NEW:
        LDA B_TEMP
        STA B_ARYPTR            ; ARY_SIZES uses B_ARYSUB for the sizes
        LDA B_TEMP2
        STA B_ARYPTR+1
        LDA B_IPTR
        PHA
        LDA B_IPTR+1
        PHA
        LDA #<TXT_ARYONE
        STA B_IPTR
        LDA #>TXT_ARYONE
        STA B_IPTR+1
        JSR ARY_SIZES           ; "10", the eleven elements of a bare A(n)
        PLA
        STA B_IPTR+1
        PLA
        STA B_IPTR
        LDA B_ARYPTR
        STA B_TEMP
        LDA B_ARYPTR+1
        STA B_TEMP2
        LDA B_DIMTOT
        STA B_TMP
        LDA B_DIMTOT+1
        STA B_TMP+1
        JSR ARY_ALLOC
        BCS ERR_OUT_OF_MEMORY
        LDA B_DIMPTR
        STA B_TMPP
        LDA B_DIMPTR+1
        STA B_TMPP+1
        LDY #4
        LDA B_DIMN              ; a name used before DIM is numeric
        STA (B_TMPP),Y
        INY
        LDA B_DIMTOT
        STA (B_TMPP),Y
        INY
        LDA B_DIMTOT+1
        STA (B_TMPP),Y
        LDA B_DIMS
        LDY #7
        STA (B_TMPP),Y
        INY
        LDA B_DIMS+1
        STA (B_TMPP),Y
        JSR ARY_LINK
        LDA B_DIMPTR
        STA B_ARYPTR
        LDA B_DIMPTR+1
        STA B_ARYPTR+1
ARY_LOOKUP_DONE:
        RTS

; ARY_ADDR: the name is in B_TEMP/B_TEMP2 and the subscripts at (B_IPTR).
; B_TMPP comes back holding the address of the element.
ARY_ADDR:
        JSR ARY_LOOKUP
        LDY #4
        LDA (B_ARYPTR),Y
        AND #$80
        STA B_ARYTYPE           ; bit 7 tells the caller it is an array of strings
        LDA #0
        STA B_ARYTMP
        STA B_ARYTMP+1
        STA B_ARYIDX
        JSR STRARG_OPEN
ARY_ADDR_LOOP:
        JSR EXPR
        JSR STRNONE
        JSR FTOI
        BCS ERR_BAD_SUBSCRIPT
        STA B_ARYSUB
        STY B_ARYSUB+1
        LDA B_ARYSUB+1
        BNE ERR_BAD_SUBSCRIPT   ; no dimension here is that large
        ; there must be a dimension for this subscript
        LDY #4
        LDA (B_ARYPTR),Y
        AND #$7F
        CMP B_ARYIDX
        BCC ERR_BAD_SUBSCRIPT
        BEQ ERR_BAD_SUBSCRIPT
        ; the size of that dimension
        LDA B_ARYIDX
        ASL A
        CLC
        ADC #7
        TAY
        LDA (B_ARYPTR),Y
        CMP B_ARYSUB
        BCC ERR_BAD_SUBSCRIPT   ; outside the array
        ; the offset grows by one whole row for each step outwards
        CLC
        ADC #1
        STA B_ARYMUL
        JSR ARY_MUL8
        LDA B_ARYTMP
        CLC
        ADC B_ARYSUB
        STA B_ARYTMP
        LDA B_ARYTMP+1
        ADC #0
        STA B_ARYTMP+1
        INC B_ARYIDX
        JSR SKIPSP
        JSR CURCHAR
        CMP #','
        BNE ARY_ADDR_END
        JSR ADV
        JMP ARY_ADDR_LOOP
ARY_ADDR_END:
        JSR STRARG_CLOSE
        LDY #4
        LDA (B_ARYPTR),Y
        AND #$7F
        CMP B_ARYIDX
        BNE ERR_BAD_SUBSCRIPT   ; the wrong number of subscripts
        ; five bytes an element
        LDA #5
        STA B_ARYMUL
        JSR ARY_MUL8
        JSR ARY_DATA
        CLC
        LDA B_ARYPTR
        ADC B_ARYTMP
        STA B_TMPP
        LDA B_ARYPTR+1
        ADC B_ARYTMP+1
        STA B_TMPP+1
        RTS

; ARY_GET: the element named at (B_IPTR) becomes FAC.
ARY_GET:
        JSR ARY_ADDR
        LDA B_ARYTYPE
        BPL ARY_GET_NUMBER
        LDY #0
        LDA (B_TMPP),Y
        STA B_STRLEN
        INY
        LDA (B_TMPP),Y
        STA B_STRPTR
        INY
        LDA (B_TMPP),Y
        STA B_STRPTR+1
        JMP STRSET
ARY_GET_NUMBER:
        LDY #0
        LDA (B_TMPP),Y
        STA B_FAC
        INY
        LDA (B_TMPP),Y
        STA B_FAC+1
        INY
        LDA (B_TMPP),Y
        STA B_FAC+2
        INY
        LDA (B_TMPP),Y
        STA B_FAC+3
        INY
        LDA (B_TMPP),Y
        STA B_FAC+4
        JMP STRCLR

; ARY_PUT: FAC goes into the element named at (B_IPTR).
ARY_PUT:
        JSR ARY_ADDR
        LDA B_ARYTYPE
        PHA                     ; the type
        LDA B_TMPP              ; and the address, both across the expression
        PHA
        LDA B_TMPP+1
        PHA
        JSR SKIPSP
        JSR CURCHAR
        CMP #'='
        BNE ERR_SYNTAX
        JSR ADV
        JSR EXPR
        PLA
        STA B_TMPP+1
        PLA
        STA B_TMPP
        PLA
        BPL ARY_PUT_NUMBER
        JMP ARY_STORE_STRING
ARY_PUT_NUMBER:
        JSR STRNONE
        LDY #0
        LDA B_FAC
        STA (B_TMPP),Y
        INY
        LDA B_FAC+1
        STA (B_TMPP),Y
        INY
        LDA B_FAC+2
        STA (B_TMPP),Y
        INY
        LDA B_FAC+3
        STA (B_TMPP),Y
        INY
        LDA B_FAC+4
        STA (B_TMPP),Y
        CLC
        RTS

; ARY_STORE_STRING: the string at B_STRPTR, B_STRLEN bytes of it, is copied into
; the heap and the element at B_TMPP is pointed at the copy.
ARY_STORE_STRING:
        JSR STRWANT
        LDA B_STRLEN
        BEQ ARY_STORE_EMPTY
        STA B_TMP
        JSR STRALLOC
        BCS ERR_OUT_OF_MEMORY
        LDY #0
ARY_STORE_COPY:
        CPY B_STRLEN
        BEQ ARY_STORE_POINT
        LDA (B_STRPTR),Y
        STA (B_STREND),Y
        INY
        JMP ARY_STORE_COPY
ARY_STORE_POINT:
        LDY #0
        LDA B_STRLEN
        STA (B_TMPP),Y
        INY
        LDA B_STREND
        STA (B_TMPP),Y
        INY
        LDA B_STREND+1
        STA (B_TMPP),Y
        CLC
        RTS
ARY_STORE_EMPTY:
        LDY #0
        LDA #0
        STA (B_TMPP),Y
        INY
        STA (B_TMPP),Y
        INY
        STA (B_TMPP),Y
        CLC
        RTS

; The sizes a name is given when it is used before it is dimensioned.
TXT_ARYONE:
        .text "(10)"
        .byte 0
