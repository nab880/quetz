#ifndef SST_MMIO_M68K_H
#define SST_MMIO_M68K_H
#include <stdint.h>

/* Extension-word bytes that follow the MOVE opcode word for a memory EA. */
static inline int m68k_ea_extlen(unsigned mode, unsigned reg)
{
    switch (mode) {
    case 2: return 0;                   /* (An)                      */
    /* (An)+ / -(An) also carry no extension words, but servicing them means
     * writing the incremented/decremented An back — which the fault handler
     * does not do (it only advances PC). Rejecting here makes such an access
     * fall through to the normal SEGV path instead of silently corrupting An. */
    case 3: case 4: return -1;          /* (An)+, -(An): unsupported */
    case 5: return 2;                   /* (d16,An)                  */
    case 6: return 2;                   /* (d8,An,Xn) brief ext      */
    case 7:
        switch (reg) {
        case 0: return 2;               /* (xxx).W                   */
        case 1: return 4;               /* (xxx).L                   */
        case 2: return 2;               /* (d16,PC)                  */
        case 3: return 2;               /* (d8,PC,Xn)                */
        default: return -1;
        }
    default: return -1;                 /* 0=Dn, 1=An: not memory    */
    }
}

/*
 * Decode an m68k MOVE.B/W/L whose memory operand is the faulting aperture
 * access (what the compiler emits for `*(volatile T *)mmio`). The other operand
 * is a data register (load or store) or an immediate (store of a constant, e.g.
 * `move.l #&scratch,(a0)`). Big-endian; `op` is the opcode word. Returns total
 * instruction length, sets *dreg (>=0 register, or -1 = immediate source whose
 * value the caller reads from guest_pc+2). Returns 0 if not a handled form.
 */
static inline int m68k_mmio_decode(uint16_t op, int *is_store, unsigned *size,
                                    int *dreg, unsigned *index_offset)
{
    *index_offset = 0;
    if ((op & 0xC000) != 0x0000) {
        return 0; /* not the MOVE family (bits[15:14] != 00) */
    }
    unsigned imm_bytes;
    switch ((op >> 12) & 0x3) {         /* MOVE size: 01=B, 11=W, 10=L */
    case 1: *size = 1; imm_bytes = 2; break;   /* immediate .B occupies a word */
    case 3: *size = 2; imm_bytes = 2; break;
    case 2: *size = 4; imm_bytes = 4; break;
    default: return 0;
    }
    unsigned dst_reg  = (op >> 9) & 0x7;
    unsigned dst_mode = (op >> 6) & 0x7;
    unsigned src_mode = (op >> 3) & 0x7;
    unsigned src_reg  = op & 0x7;
    int dst_is_mem = (dst_mode >= 2 && (dst_mode != 7 || dst_reg <= 1));
    if (*size == 1 && (src_mode == 1 || dst_mode == 1)) {
        return 0; /* MOVE.B cannot use an address register. */
    }

    /* The data operand is a data register (Dn, mode 0) or an address register
     * (An, mode 1; MOVEA / move from An). *dreg encodes it: 0-7 = Dn,
     * 8-15 = An, -1 = immediate source. */
    if ((src_mode == 0 || src_mode == 1) && dst_is_mem) {
        int ext = m68k_ea_extlen(dst_mode, dst_reg);   /* reg -> memory */
        if (ext < 0) {
            return 0;
        }
        if (dst_mode == 6) *index_offset = 2;
        *is_store = 1;
        *dreg = (src_mode == 1) ? (int)(8 + src_reg) : (int)src_reg;
        return 2 + ext;
    }
    if (src_mode == 7 && src_reg == 4 && dst_is_mem) {
        int ext = m68k_ea_extlen(dst_mode, dst_reg);   /* #imm -> memory */
        if (ext < 0) {
            return 0;
        }
        /* source immediate precedes the destination EA extension words */
        if (dst_mode == 6) *index_offset = 2 + imm_bytes;
        *is_store = 1; *dreg = -1; return 2 + (int)imm_bytes + ext;
    }
    if (dst_mode == 0 || dst_mode == 1) {
        int ext = m68k_ea_extlen(src_mode, src_reg);   /* memory -> reg */
        if (ext < 0) {
            return 0;
        }
        if (src_mode == 6 || (src_mode == 7 && src_reg == 3))
            *index_offset = 2;
        *is_store = 0;
        *dreg = (dst_mode == 1) ? (int)(8 + dst_reg) : (int)dst_reg;
        return 2 + ext;
    }
    return 0;
}

/* MOVE sets N/Z from the operand width, clears V/C, and preserves X.
 * MOVEA does not call this helper: every condition code remains unchanged. */
static inline uint32_t m68k_mmio_move_ccr(uint32_t value, unsigned size,
                                         uint32_t previous_ccr)
{
    const uint32_t mask = size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1u;
    value &= mask;
    return (previous_ccr & 0x10u) | (value == 0 ? 0x04u : 0u) |
           (value & (1u << (size * 8 - 1)) ? 0x08u : 0u);
}
#endif
