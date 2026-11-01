; SPDX-License-Identifier: MIT
;
; fp.s -- the Kestrel BASIC's arithmetic.
;
; A number is five bytes: an exponent, then a four-byte mantissa whose top bit
; is the sign. The value is
;
;       (mantissa / 2^32) * 2^(exponent - 128)
;
; normalised so the mantissa's most significant bit is set -- that is,
; 0.5 <= |m| < 1.0. All zeroes are zero. The exponent range gives about
; 2^-128 to 2^127, and the 32-bit mantissa about nine significant digits,
; which is what a 1982 machine's BASIC had.
;
; The working registers are B_WORK: A's magnitude at +0, B's at +4, and the
; 64-bit product at +8.

; ---------------------------------------------------------------------------
; Small helpers on the working registers
; ---------------------------------------------------------------------------

; SHL_A: B_WORK+0..3 <<= 1, carry out in the carry flag.
SHL_A:
        ASL B_WORK+3
        ROL B_WORK+2
        ROL B_WORK+1
        ROL B_WORK
        RTS

; SHR_A: B_WORK+0..3 >>= 1, the bit that fell off in the carry.
SHR_A:
        LSR B_WORK
        ROR B_WORK+1
        ROR B_WORK+2
        ROR B_WORK+3
        RTS

; SHL_B: B_WORK+4..7 <<= 1.
SHL_B:
        ASL B_WORK+7
        ROL B_WORK+6
        ROL B_WORK+5
        ROL B_WORK+4
        RTS

; SHR_B: B_WORK+4..7 >>= 1.
SHR_B:
        LSR B_WORK+4
        ROR B_WORK+5
        ROR B_WORK+6
        ROR B_WORK+7
        RTS

; ADD_A_B: A += B. Carry set when the sum overflowed four bytes.
ADD_A_B:
        CLC
        LDA B_WORK+3
        ADC B_WORK+7
        STA B_WORK+3
        LDA B_WORK+2
        ADC B_WORK+6
        STA B_WORK+2
        LDA B_WORK+1
        ADC B_WORK+5
        STA B_WORK+1
        LDA B_WORK
        ADC B_WORK+4
        STA B_WORK
        RTS

; SUB_A_B: A -= B.
SUB_A_B:
        SEC
        LDA B_WORK+3
        SBC B_WORK+7
        STA B_WORK+3
        LDA B_WORK+2
        SBC B_WORK+6
        STA B_WORK+2
        LDA B_WORK+1
        SBC B_WORK+5
        STA B_WORK+1
        LDA B_WORK
        SBC B_WORK+4
        STA B_WORK
        RTS

; CMP_A_B: compares the magnitudes. A holds one of 0 (equal), 1 (A > B) or
; $FF (A < B), and the carry is set when A >= B.
CMP_A_B:
        LDA B_WORK
        CMP B_WORK+4
        BNE CMP_A_B_DIFF
        LDA B_WORK+1
        CMP B_WORK+5
        BNE CMP_A_B_DIFF
        LDA B_WORK+2
        CMP B_WORK+6
        BNE CMP_A_B_DIFF
        LDA B_WORK+3
        CMP B_WORK+7
        BNE CMP_A_B_DIFF
        LDA #0
        RTS
CMP_A_B_DIFF:
        BCS CMP_A_B_GT
        LDA #$FF
        CLC
        RTS
CMP_A_B_GT:
        LDA #1
        SEC
        RTS

; LOADFAC: FAC's mantissa, without its sign, into A.
LOADFAC:
        LDA B_FAC+1
        AND #$7F
        ORA #$80
        STA B_WORK
        LDA B_FAC+2
        STA B_WORK+1
        LDA B_FAC+3
        STA B_WORK+2
        LDA B_FAC+4
        STA B_WORK+3
        RTS

; LOADARG: ARG's mantissa, without its sign, into B.
LOADARG:
        LDA B_ARG+1
        AND #$7F
        ORA #$80
        STA B_WORK+4
        LDA B_ARG+2
        STA B_WORK+5
        LDA B_ARG+3
        STA B_WORK+6
        LDA B_ARG+4
        STA B_WORK+7
        RTS

; ---------------------------------------------------------------------------
; Zero, copy, negate, absolute value
; ---------------------------------------------------------------------------

FZERO:
        LDA #0
        STA B_FAC
        STA B_FAC+1
        STA B_FAC+2
        STA B_FAC+3
        STA B_FAC+4
        RTS

FCOPY_ARG:
        LDA B_ARG
        STA B_FAC
        LDA B_ARG+1
        STA B_FAC+1
        LDA B_ARG+2
        STA B_FAC+2
        LDA B_ARG+3
        STA B_FAC+3
        LDA B_ARG+4
        STA B_FAC+4
        RTS

FNEG:
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BEQ FNEG_DONE
        LDA B_FAC+1
        EOR #$80
        STA B_FAC+1
FNEG_DONE:
        RTS

FABS:
        LDA B_FAC+1
        AND #$7F
        STA B_FAC+1
        RTS

; ---------------------------------------------------------------------------
; Addition and subtraction
; ---------------------------------------------------------------------------

FSUB:
        JSR FNEG
        JSR FADD
        JMP FNEG

; FADD: FAC = FAC + ARG.
FADD:
        LDA B_ARG
        ORA B_ARG+1
        ORA B_ARG+2
        ORA B_ARG+3
        ORA B_ARG+4
        BEQ FADD_DONE
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BNE FADD_BOTH
        JMP FCOPY_ARG
FADD_DONE:
        RTS
FADD_BOTH:
        LDA B_FAC+1
        AND #$80
        STA B_SIGN
        LDA B_ARG+1
        AND #$80
        STA B_TMP3
        JSR LOADFAC
        JSR LOADARG

        LDA B_FAC
        CMP B_ARG
        BCS FADD_FAC_BIGGER
        LDA B_ARG
        SEC
        SBC B_FAC
        STA B_EXP
        LDA B_ARG
        STA B_DIGITS
        JSR SHIFT_A_RIGHT
        JMP FADD_SIGNS
FADD_FAC_BIGGER:
        LDA B_FAC
        SEC
        SBC B_ARG
        STA B_EXP
        LDA B_FAC
        STA B_DIGITS
        JSR SHIFT_B_RIGHT
FADD_SIGNS:
        LDA B_SIGN
        CMP B_TMP3
        BEQ FADD_SAME
        JSR CMP_A_B
        BEQ FADD_RESULT_ZERO
        BCC FADD_B_BIGGER
        JSR SUB_A_B
        LDA B_SIGN
        STA B_TY
        JMP FADD_NORMALISE
FADD_B_BIGGER:
        JSR SWAP_AB
        JSR SUB_A_B
        LDA B_TMP3
        STA B_TY
        JMP FADD_NORMALISE
FADD_SAME:
        JSR ADD_A_B
        LDA B_SIGN
        STA B_TY
        BCC FADD_NORMALISE
        JSR SHR_A
        LDA B_WORK
        ORA #$80
        STA B_WORK
        INC B_DIGITS
        BEQ FADD_OVERFLOW
FADD_NORMALISE:
        LDA B_WORK
        ORA B_WORK+1
        ORA B_WORK+2
        ORA B_WORK+3
        BEQ FADD_RESULT_ZERO
FADD_NORM_LOOP:
        LDA B_WORK
        BMI FADD_STORE
        JSR SHL_A
        DEC B_DIGITS
        BEQ FADD_RESULT_ZERO
        JMP FADD_NORM_LOOP
FADD_STORE:
        LDA B_DIGITS
        STA B_FAC
        LDA B_WORK
        AND #$7F
        ORA B_TY
        STA B_FAC+1
        LDA B_WORK+1
        STA B_FAC+2
        LDA B_WORK+2
        STA B_FAC+3
        LDA B_WORK+3
        STA B_FAC+4
        RTS
FADD_RESULT_ZERO:
        JMP FZERO
FADD_OVERFLOW:
        JMP FZERO              ; the caller turns a silent zero into ?OVERFLOW

; SHIFT_A_RIGHT: A >>= B_EXP, dropping what falls off the end.
SHIFT_A_RIGHT:
        LDA B_EXP
        CMP #32
        BCS SHIFT_A_DROP
        TAX
        BEQ SHIFT_A_RIGHT_RTS
SHIFT_A_RIGHT_LOOP:
        JSR SHR_A
        DEX
        BNE SHIFT_A_RIGHT_LOOP
SHIFT_A_RIGHT_RTS:
        RTS
SHIFT_A_DROP:
        LDA #0
        STA B_WORK
        STA B_WORK+1
        STA B_WORK+2
        STA B_WORK+3
        RTS

; SHIFT_B_RIGHT: B >>= B_EXP.
SHIFT_B_RIGHT:
        LDA B_EXP
        CMP #32
        BCS SHIFT_B_DROP
        TAX
        BEQ SHIFT_B_RIGHT_RTS
SHIFT_B_RIGHT_LOOP:
        JSR SHR_B
        DEX
        BNE SHIFT_B_RIGHT_LOOP
SHIFT_B_RIGHT_RTS:
        RTS
SHIFT_B_DROP:
        LDA #0
        STA B_WORK+4
        STA B_WORK+5
        STA B_WORK+6
        STA B_WORK+7
        RTS

; SWAP_AB: exchange the two working magnitudes.
SWAP_AB:
        LDX #0
SWAP_AB_LOOP:
        LDA B_WORK,X
        PHA
        LDA B_WORK+4,X
        STA B_WORK,X
        PLA
        STA B_WORK+4,X
        INX
        CPX #4
        BNE SWAP_AB_LOOP
        RTS

; ---------------------------------------------------------------------------
; Multiplication
; ---------------------------------------------------------------------------

; FMUL: FAC = FAC * ARG.
FMUL:
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BEQ FMUL_ZERO
        LDA B_ARG
        ORA B_ARG+1
        ORA B_ARG+2
        ORA B_ARG+3
        ORA B_ARG+4
        BEQ FMUL_ZERO
        LDA B_FAC+1
        AND #$80
        STA B_SIGN
        LDA B_ARG+1
        AND #$80
        EOR B_SIGN
        STA B_TY
        JSR LOADFAC
        JSR LOADARG

        ; The exponent adds: value = mF*mA * 2^(eF+eA-256), and the product
        ; mF*mA/2^64 keeps the same meaning, so the stored exponent is
        ; eF+eA-128.
        LDA B_FAC
        CLC
        ADC B_ARG
        SEC
        SBC #128
        STA B_DIGITS

        ; 32 rounds of shift and add: P <<= 1, and if the top bit of B is set,
        ; P's low half gains A.
        LDA #0
        STA B_WORK+8
        STA B_WORK+9
        STA B_WORK+10
        STA B_WORK+11
        STA B_WORK+12
        STA B_WORK+13
        STA B_WORK+14
        STA B_WORK+15
        LDX #32
FMUL_LOOP:
        ASL B_WORK+15
        ROL B_WORK+14
        ROL B_WORK+13
        ROL B_WORK+12
        ROL B_WORK+11
        ROL B_WORK+10
        ROL B_WORK+9
        ROL B_WORK+8
        LDA B_WORK+4
        BPL FMUL_NO_ADD
        CLC
        LDA B_WORK+15
        ADC B_WORK+3
        STA B_WORK+15
        LDA B_WORK+14
        ADC B_WORK+2
        STA B_WORK+14
        LDA B_WORK+13
        ADC B_WORK+1
        STA B_WORK+13
        LDA B_WORK+12
        ADC B_WORK
        STA B_WORK+12
        LDA B_WORK+11
        ADC #0
        STA B_WORK+11
        LDA B_WORK+10
        ADC #0
        STA B_WORK+10
        LDA B_WORK+9
        ADC #0
        STA B_WORK+9
        LDA B_WORK+8
        ADC #0
        STA B_WORK+8
FMUL_NO_ADD:
        ASL B_WORK+7
        ROL B_WORK+6
        ROL B_WORK+5
        ROL B_WORK+4
        DEX
        BNE FMUL_LOOP

        ; The top four bytes of the product are the mantissa, in the low end of
        ; its range; normalise them up.
        LDA B_WORK+8
        STA B_WORK
        LDA B_WORK+9
        STA B_WORK+1
        LDA B_WORK+10
        STA B_WORK+2
        LDA B_WORK+11
        STA B_WORK+3
        LDA B_WORK
        ORA B_WORK+1
        ORA B_WORK+2
        ORA B_WORK+3
        BEQ FMUL_ZERO
FMUL_NORM:
        LDA B_WORK
        BMI FMUL_STORE
        JSR SHL_A
        DEC B_DIGITS
        BEQ FMUL_ZERO
        JMP FMUL_NORM
FMUL_STORE:
        LDA B_DIGITS
        STA B_FAC
        LDA B_WORK
        AND #$7F
        ORA B_TY
        STA B_FAC+1
        LDA B_WORK+1
        STA B_FAC+2
        LDA B_WORK+2
        STA B_FAC+3
        LDA B_WORK+3
        STA B_FAC+4
        RTS
FMUL_ZERO:
        JMP FZERO

; ---------------------------------------------------------------------------
; Division
; ---------------------------------------------------------------------------

; FDIV: FAC = FAC / ARG.
FDIV:
        LDA B_ARG
        ORA B_ARG+1
        ORA B_ARG+2
        ORA B_ARG+3
        ORA B_ARG+4
        BEQ FDIV_BY_ZERO
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BEQ FDIV_ZERO
        LDA B_FAC+1
        AND #$80
        STA B_SIGN
        LDA B_ARG+1
        AND #$80
        EOR B_SIGN
        STA B_TY
        JSR LOADFAC
        JSR LOADARG

        LDA B_FAC
        SEC
        SBC B_ARG
        CLC
        ADC #128
        STA B_DIGITS

        ; The long division wants |A| < |B|; when it is not, A is halved and
        ; the exponent carries the difference.
        JSR CMP_A_B
        BCC FDIV_READY
        JSR SHR_A
        INC B_DIGITS
FDIV_READY:
        ; R (five bytes) = A, so the extra bit has somewhere to go.
        LDA #0
        STA B_FPBUF
        LDA B_WORK
        STA B_FPBUF+1
        LDA B_WORK+1
        STA B_FPBUF+2
        LDA B_WORK+2
        STA B_FPBUF+3
        LDA B_WORK+3
        STA B_FPBUF+4
        LDA #0
        STA B_WORK
        STA B_WORK+1
        STA B_WORK+2
        STA B_WORK+3
        LDX #32
FDIV_LOOP:
        ASL B_FPBUF+4
        ROL B_FPBUF+3
        ROL B_FPBUF+2
        ROL B_FPBUF+1
        ROL B_FPBUF
        ASL B_WORK+3
        ROL B_WORK+2
        ROL B_WORK+1
        ROL B_WORK
        ; R -= B, keeping it when the subtraction did not go negative.
        LDA B_FPBUF+4
        SEC
        SBC B_WORK+7
        TAY
        LDA B_FPBUF+3
        SBC B_WORK+6
        STA B_TMP
        LDA B_FPBUF+2
        SBC B_WORK+5
        STA B_TMP2
        LDA B_FPBUF+1
        SBC B_WORK+4
        STA B_TMP3
        LDA B_FPBUF
        SBC #0
        BCC FDIV_NO_SUB
        STA B_FPBUF
        LDA B_TMP3
        STA B_FPBUF+1
        LDA B_TMP2
        STA B_FPBUF+2
        LDA B_TMP
        STA B_FPBUF+3
        STY B_FPBUF+4
        LDA B_WORK+3
        ORA #1
        STA B_WORK+3
FDIV_NO_SUB:
        DEX
        BNE FDIV_LOOP
        ; The quotient is in A; normalise it.
        LDA B_WORK
        ORA B_WORK+1
        ORA B_WORK+2
        ORA B_WORK+3
        BEQ FDIV_ZERO
FDIV_NORM:
        LDA B_WORK
        BMI FDIV_STORE
        JSR SHL_A
        DEC B_DIGITS
        BEQ FDIV_ZERO
        JMP FDIV_NORM
FDIV_STORE:
        LDA B_DIGITS
        STA B_FAC
        LDA B_WORK
        AND #$7F
        ORA B_TY
        STA B_FAC+1
        LDA B_WORK+1
        STA B_FAC+2
        LDA B_WORK+2
        STA B_FAC+3
        LDA B_WORK+3
        STA B_FAC+4
        CLC
        RTS
FDIV_ZERO:
        JSR FZERO
        CLC
        RTS
FDIV_BY_ZERO:
        JSR FZERO
        SEC
        RTS

; ---------------------------------------------------------------------------
; Comparing
; ---------------------------------------------------------------------------

; FCOMP: A = $FF when FAC < ARG, 0 when equal, 1 when FAC > ARG, with the
; carry set when FAC >= ARG.
FCOMP:
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BEQ FCOMP_FAC_ZERO
        LDA B_ARG
        ORA B_ARG+1
        ORA B_ARG+2
        ORA B_ARG+3
        ORA B_ARG+4
        BEQ FCOMP_ARG_ZERO
        ; Signs first: a negative is below a positive, and two negatives
        ; compare the other way round.
        LDA B_FAC+1
        EOR B_ARG+1
        BPL FCOMP_SAME_SIGN
        LDA B_FAC+1
        BMI FCOMP_LT
        JMP FCOMP_GT
FCOMP_SAME_SIGN:
        ; The exponents decide unless they are equal, and only then do the
        ; mantissas matter -- 4 and 1 have the same mantissa and differ only in
        ; their exponent.
        LDA B_FAC
        CMP B_ARG
        BNE FCOMP_EXP_ORDER
        JSR LOADFAC
        JSR LOADARG
        JSR CMP_A_B
        BNE FCOMP_MAG_ORDER
        LDA #0
        SEC
        RTS
FCOMP_EXP_ORDER:
        BCS FCOMP_MAG_GT
        JMP FCOMP_MAG_LT
FCOMP_MAG_ORDER:
        BCS FCOMP_MAG_GT
FCOMP_MAG_LT:
        LDA B_FAC+1
        BMI FCOMP_GT
        JMP FCOMP_LT
FCOMP_MAG_GT:
        LDA B_FAC+1
        BMI FCOMP_LT
        JMP FCOMP_GT
FCOMP_FAC_ZERO:
        LDA B_ARG
        ORA B_ARG+1
        ORA B_ARG+2
        ORA B_ARG+3
        ORA B_ARG+4
        BEQ FCOMP_EQUAL
        LDA B_ARG+1
        BMI FCOMP_GT          ; zero is above a negative
        JMP FCOMP_LT
FCOMP_ARG_ZERO:
        LDA B_FAC+1
        BMI FCOMP_LT
        JMP FCOMP_GT
FCOMP_EQUAL:
        LDA #0
        SEC
        RTS
FCOMP_GT:
        LDA #1
        SEC
        RTS
FCOMP_LT:
        LDA #$FF
        CLC
        RTS

; ---------------------------------------------------------------------------
; Integer conversion
; ---------------------------------------------------------------------------

; ITOF: the signed 16-bit number in A (low) and Y (high) becomes FAC.
ITOF:
        STA B_INT
        STY B_INT+1
        JSR FZERO
        LDA B_INT+1
        BPL ITOF_POSITIVE
        ; Negate the magnitude, remembering the sign.
        LDA #$80
        STA B_SIGN
        SEC
        LDA #0
        SBC B_INT
        STA B_INT
        LDA #0
        SBC B_INT+1
        STA B_INT+1
        JMP ITOF_BUILD
ITOF_POSITIVE:
        LDA #0
        STA B_SIGN
ITOF_BUILD:
        LDA B_INT
        ORA B_INT+1
        BEQ ITOF_DONE
        LDA B_INT+1
        STA B_WORK
        LDA B_INT
        STA B_WORK+1
        LDA #0
        STA B_WORK+2
        STA B_WORK+3
        LDA #128+16
        STA B_DIGITS
ITOF_NORM:
        LDA B_WORK
        BMI ITOF_STORE
        JSR SHL_A
        DEC B_DIGITS
        JMP ITOF_NORM
ITOF_STORE:
        LDA B_DIGITS
        STA B_FAC
        LDA B_WORK
        AND #$7F
        ORA B_SIGN
        STA B_FAC+1
        LDA B_WORK+1
        STA B_FAC+2
        LDA B_WORK+2
        STA B_FAC+3
        LDA B_WORK+3
        STA B_FAC+4
ITOF_DONE:
        RTS

; FTOI: FAC becomes the signed 16-bit integer in A (low) and Y (high), rounded
; towards zero. Carry set when the value did not fit.
FTOI:
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BEQ FTOI_ZERO
        ; Anything at or above 2^17 cannot come back as sixteen bits.
        LDA B_FAC
        CMP #128+17
        BCS FTOI_RANGE
        JSR LOADFAC
        ; Shift the mantissa so that the integer part sits in the high word.
        LDA #128+16
        SEC
        SBC B_FAC
        BEQ FTOI_SHIFTED
        TAX
FTOI_SHIFT:
        JSR SHR_A
        DEX
        BNE FTOI_SHIFT
FTOI_SHIFTED:
        LDA B_WORK+1
        STA B_INT
        LDA B_WORK
        STA B_INT+1
        LDA B_FAC+1
        BPL FTOI_POSITIVE
        ; Two's complement, and the answer is left in B_INT as well so that the
        ; registers and memory always agree.
        SEC
        LDA #0
        SBC B_INT
        STA B_TMP
        LDA #0
        SBC B_INT+1
        STA B_INT+1
        LDA B_TMP
        STA B_INT
        LDY B_INT+1
        CLC
        RTS
FTOI_POSITIVE:
        LDA B_INT
        LDY B_INT+1
        CLC
        RTS
FTOI_ZERO:
        LDA #0
        TAY
        CLC
        RTS
FTOI_RANGE:
        LDA #0
        TAY
        SEC
        RTS

; ---------------------------------------------------------------------------
; INT, SGN, SQR
; ---------------------------------------------------------------------------

; FINT: FAC = the largest integer that is not greater than FAC.
FINT:
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BEQ FINT_DONE
        LDA B_FAC
        CMP #128+16
        BCS FINT_DONE           ; this size has no fraction to lose
        CMP #128
        BCS FINT_POSITIVE
        ; Below one: zero, or minus one when negative.
        LDA B_FAC+1
        BPL FINT_TO_ZERO
        LDA #129
        STA B_FAC
        LDA #$80
        STA B_FAC+1
        LDA #0
        STA B_FAC+2
        STA B_FAC+3
        STA B_FAC+4
        RTS
FINT_TO_ZERO:
        JMP FZERO
FINT_POSITIVE:
        JSR LOADFAC
        ; Shift the mantissa until the integer part fills the high word; what
        ; leaves the bottom of it is the fraction, which INT drops.
        LDA #128+16
        SEC
        SBC B_FAC
        TAX
        LDA #0
        STA B_TMP
FINT_SHIFT:
        JSR SHR_A
        LDA B_WORK+3
        AND #1
        ORA B_TMP
        STA B_TMP
        DEX
        BNE FINT_SHIFT
        LDA #0
        STA B_WORK+2
        STA B_WORK+3
        ; A negative number floors downwards when a fraction was dropped, and
        ; the mantissa here is a magnitude, so that means one more, not less.
        LDA B_FAC+1
        BPL FINT_HALVED
        LDA B_TMP
        BEQ FINT_HALVED
        INC B_WORK+1
        BNE FINT_HALVED
        INC B_WORK
FINT_HALVED:
        LDA #0
        STA B_TY
        LDA #128+16
        STA B_DIGITS
        LDA B_WORK
        ORA B_WORK+1
        BEQ FINT_TO_ZERO
FINT_NORM:
        LDA B_WORK
        BMI FINT_STORE
        JSR SHL_A
        DEC B_DIGITS
        JMP FINT_NORM
FINT_STORE:
        LDA B_FAC+1
        AND #$80
        STA B_TY
        LDA B_DIGITS
        STA B_FAC
        LDA B_WORK
        AND #$7F
        ORA B_TY
        STA B_FAC+1
        LDA B_WORK+1
        STA B_FAC+2
        LDA B_WORK+2
        STA B_FAC+3
        LDA B_WORK+3
        STA B_FAC+4
FINT_DONE:
        RTS

; FSGN: FAC = -1, 0 or 1 by its sign.
FSGN:
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BEQ FSGN_ZERO
        LDA #1
        LDY #0
        LDX B_FAC+1
        BPL FSGN_STORE
        LDA #$FF
        TAY
FSGN_STORE:
        JMP ITOF
FSGN_ZERO:
        JMP FZERO

; FSQR: FAC = the square root of FAC, by Newton's method. Six steps from a
; guess that halves the exponent is one digit more than the mantissa carries.
FSQR:
        LDA B_FAC
        ORA B_FAC+1
        ORA B_FAC+2
        ORA B_FAC+3
        ORA B_FAC+4
        BEQ FSQR_DONE
        LDA B_FAC+1
        BPL FSQR_START
        JMP FZERO               ; the caller reports ?ILLEGAL QUANTITY
FSQR_START:
        ; Keep the radicand: every step needs it again.
        LDA B_FAC
        STA B_SQRBUF
        LDA B_FAC+1
        STA B_SQRBUF+1
        LDA B_FAC+2
        STA B_SQRBUF+2
        LDA B_FAC+3
        STA B_SQRBUF+3
        LDA B_FAC+4
        STA B_SQRBUF+4
        ; The guess: the same mantissa with half the exponent. The difference
        ; from the bias is signed, so it is halved arithmetically.
        LDA B_FAC
        SEC
        SBC #128
        CMP #$80
        ROR A
        CLC
        ADC #128
        STA B_ARG
        LDA B_FAC+1
        STA B_ARG+1
        LDA B_FAC+2
        STA B_ARG+2
        LDA B_FAC+3
        STA B_ARG+3
        LDA B_FAC+4
        STA B_ARG+4
        LDA #8
        STA B_LOOPCNT
FSQR_LOOP:
        LDA B_SQRBUF
        STA B_FAC
        LDA B_SQRBUF+1
        STA B_FAC+1
        LDA B_SQRBUF+2
        STA B_FAC+2
        LDA B_SQRBUF+3
        STA B_FAC+3
        LDA B_SQRBUF+4
        STA B_FAC+4
        JSR FDIV                ; FAC = radicand / guess
        JSR FADD                ; FAC = radicand / guess + guess
        DEC B_FAC               ; and half of that
        LDA B_FAC
        STA B_ARG
        LDA B_FAC+1
        STA B_ARG+1
        LDA B_FAC+2
        STA B_ARG+2
        LDA B_FAC+3
        STA B_ARG+3
        LDA B_FAC+4
        STA B_ARG+4
        DEC B_LOOPCNT
        BNE FSQR_LOOP
FSQR_DONE:
        RTS
