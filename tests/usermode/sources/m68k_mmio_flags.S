/* Freestanding Linux regression: branch immediately after every emulated MOVE.
 * The mailbox peer returns zero at +0, one at +4, and the width's sign bit at +8.
 * Stores to +16 must set the same flags. MOVEA must preserve all 32 CCR values. */
    .text
    .globl _start

    .macro check kind, x
    .ifc \kind,zero
        bne fail
        bmi fail
    .endif
    .ifc \kind,pos
        beq fail
        bmi fail
    .endif
    .ifc \kind,neg
        beq fail
        bpl fail
    .endif
        bvs fail
        bcs fail
        move.w %ccr,%d2
        andi.w #16,%d2
        cmpi.w #\x,%d2
        bne fail
    .endm

    .macro transfer width, kind, offset, value, seed, x
        move.w #(\seed+\x),%ccr
        move.\width \offset(%a0),%d0
        check \kind,\x
        move.l #\value,%d0
        move.w #(\seed+\x),%ccr
        move.\width %d0,16(%a0)
        check \kind,\x
    .endm

    .macro widths x
        transfer b,zero,0,0,11,\x
        transfer w,zero,0,0,11,\x
        transfer l,zero,0,0,11,\x
        transfer b,pos,4,1,15,\x
        transfer w,pos,4,1,15,\x
        transfer l,pos,4,1,15,\x
        transfer b,neg,8,0x80,7,\x
        transfer w,neg,8,0x8000,7,\x
        transfer l,neg,8,0x80000000,7,\x
    .endm
_start:
    lea 0x70000000,%a0
    widths 0
    widths 16
    moveq #31,%d7
1:
    move.w %d7,%ccr
    movea.w 8(%a0),%a1
    move.w %ccr,%d2
    cmp.w %d7,%d2
    bne fail
    cmpa.l #0xffff8000,%a1
    bne fail
    move.w %d7,%ccr
    movea.l 8(%a0),%a1
    move.w %ccr,%d2
    cmp.w %d7,%d2
    bne fail
    cmpa.l #0x80000000,%a1
    bne fail
    dbra %d7,1b
    moveq #0,%d1
    bra exit
fail:
    moveq #1,%d1
exit:
    moveq #1,%d0
    trap #0
