/*
 * linux-user synchronous MMIO for Quetz (P6).
 *
 * In system mode the sst-mmio-bridge device intercepts the doorbell aperture;
 * user mode has no device map, so we reserve the aperture as PROT_NONE and route
 * the resulting SIGSEGV through the same sync mailbox. Ported to QEMU 9.2.1:
 * the fault is handled inside host_sigsegv_handler via cpu_restore_state (to
 * recover the guest PC) + cpu_loop_exit (to resume), not by returning from the
 * host signal handler.
 *
 * Decoders: RV64 (base + RVC compressed) and big-endian m68k (Dn/An/immediate
 * operands across the common EA modes).
 */

#include "qemu/osdep.h"
#include "qemu.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "user-mmap.h"
#include "sst_mmio.h"
#include "quetz/quetz_ipc_client.h"
#include "quetz/quetz_ipc_types.h"

#include <sys/mman.h>

#define MAX_RANGES 8
static struct SstMmioRange ranges[MAX_RANGES];
static int range_count;
/* Registration/attachment occurs before guest threads are started. */
static G_NORETURN void invalid_range(const char *reason)
{
    fprintf(stderr, "quetz: invalid MMIO aperture: %s\n", reason);
    exit(EXIT_FAILURE);
}

static uint64_t range_number(const char *value)
{
    char *end;
    if (!*value || *value == '-') {
        invalid_range("expected an unsigned number");
    }
    errno = 0;
    uint64_t n = strtoull(value, &end, 0);
    if (errno || *end) {
        invalid_range("invalid numeric field");
    }
    return n;
}

void sst_mmio_register_range(const char *spec)
{
    if (range_count >= MAX_RANGES || !spec) {
        invalid_range("too many ranges or missing specification");
    }
    struct SstMmioRange *r = &ranges[range_count];
    memset(r, 0, sizeof(*r));
    char *copy = g_strdup(spec);
    char *save = NULL;
    for (char *tok = strtok_r(copy, ",", &save); tok;
         tok = strtok_r(NULL, ",", &save)) {
        if (strncmp(tok, "shmname=", 8) == 0) {
            if (strlen(tok + 8) >= sizeof(r->shmname)) {
                invalid_range("shared-memory name too long");
            }
            g_strlcpy(r->shmname, tok + 8, sizeof(r->shmname));
        } else if (strncmp(tok, "base=", 5) == 0) {
            r->base = range_number(tok + 5);
        } else if (strncmp(tok, "size=", 5) == 0) {
            r->size = range_number(tok + 5);
        } else if (strncmp(tok, "vcpu_id=", 8) == 0) {
            uint64_t vcpu = range_number(tok + 8);
            if (vcpu > UINT_MAX) {
                invalid_range("vCPU index out of range");
            }
            r->vcpu_id = vcpu;
        } else {
            invalid_range("unknown field");
        }
    }
    g_free(copy);
    if (!r->shmname[0] || !r->size ||
        r->base > (abi_ulong)-1 || r->size > (abi_ulong)-1 ||
        r->size - 1 > (abi_ulong)-1 - r->base) {
        invalid_range("missing name, empty size, or address overflow");
    }
    for (int i = 0; i < range_count; ++i) {
        const struct SstMmioRange *old = &ranges[i];
        if ((r->base >= old->base && r->base - old->base < old->size) ||
            (old->base >= r->base && old->base - r->base < r->size)) {
            invalid_range("overlapping ranges");
        }
        if (strcmp(old->shmname, r->shmname) == 0) {
            r->client = old->client;
        }
    }
    if (!r->client) {
        r->client = quetz_ipc_attach(r->shmname);
    }
    if (r->vcpu_id != 0) {
        invalid_range("user-mode mailboxes use CPU indices; vcpu_id must be zero");
    }
    if (!r->client || !quetz_ipc_vcpu_count(r->client) ||
        quetz_ipc_vcpu_count(r->client) > QUETZ_MAX_MMIO_VCORES) {
        invalid_range("cannot attach shared memory or invalid vCPU index");
    }
    ++range_count;
}

/*
 * Reserve each aperture in the guest address space as PROT_NONE so guest
 * loads/stores fault. Must run after guest_base is established (post
 * target_cpu_copy_regs).
 */
void sst_mmio_apply_reservation(void)
{
    for (int i = 0; i < range_count; i++) {
        if (ranges[i].size == 0) {
            continue;
        }
        abi_ulong base = (abi_ulong)ranges[i].base;
        abi_ulong size = (abi_ulong)ranges[i].size;
        abi_long rv = target_mmap(base, size, PROT_NONE,
                                  MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
                                  -1, 0);
        if (rv == -1) {
            invalid_range("failed to reserve guest address space");
        }
    }
}

static const struct SstMmioRange *find_range(uint64_t addr)
{
    for (int i = 0; i < range_count; i++) {
        if (addr >= ranges[i].base &&
            addr - ranges[i].base < ranges[i].size) {
            return &ranges[i];
        }
    }
    return NULL;
}

#if defined(TARGET_RISCV64)
/*
 * Decode an RV load/store at the fault PC; returns the instruction length
 * (2 for compressed, 4 for base) or 0 if it is not a recognized load/store.
 * Only the data register + size + direction matter — the faulting address is
 * already known from the SIGSEGV.
 */
static int decode_ldst(uint32_t insn, int *is_store, unsigned *size,
                       int *rd, int *rs2, int *is_signed)
{
    *is_signed = 0;
    /* Compressed (RVC), quadrant 0: C.LW/C.LD/C.SW/C.SD (pointer-register
     * form — what the compiler emits for `*(volatile T *)mmio`). The 3-bit
     * register field maps to x8..x15. */
    if ((insn & 0x3) != 0x3) {
        unsigned cq = insn & 0x3;
        unsigned cfunct3 = (insn >> 13) & 0x7;
        unsigned creg = 8 + ((insn >> 2) & 0x7);
        if (cq != 0) {
            return 0; /* q1 = arithmetic, q2 = sp-relative (not aperture) */
        }
        switch (cfunct3) {
        case 2: *is_store = 0; *size = 4; *rd = creg; *is_signed = 1; return 2; /* C.LW */
        case 3: *is_store = 0; *size = 8; *rd  = creg; return 2; /* C.LD */
        case 6: *is_store = 1; *size = 4; *rs2 = creg; return 2; /* C.SW */
        case 7: *is_store = 1; *size = 8; *rs2 = creg; return 2; /* C.SD */
        default: return 0; /* C.FLD/C.FSD etc */
        }
    }

    unsigned opcode = insn & 0x7f;
    unsigned funct3 = (insn >> 12) & 7;
    *rd = (insn >> 7) & 0x1f;
    *rs2 = (insn >> 20) & 0x1f;
    if (opcode == 0x03) { /* LOAD */
        *is_store = 0;
        switch (funct3) {
        case 0: *size = 1; *is_signed = 1; return 4; /* LB  */
        case 1: *size = 2; *is_signed = 1; return 4; /* LH  */
        case 2: *size = 4; *is_signed = 1; return 4; /* LW  */
        case 3: *size = 8; return 4; /* LD  */
        case 4: *size = 1; return 4; /* LBU */
        case 5: *size = 2; return 4; /* LHU */
        case 6: *size = 4; return 4; /* LWU */
        default: return 0;
        }
    }
    if (opcode == 0x23) { /* STORE */
        *is_store = 1;
        switch (funct3) {
        case 0: *size = 1; return 4; /* SB */
        case 1: *size = 2; return 4; /* SH */
        case 2: *size = 4; return 4; /* SW */
        case 3: *size = 8; return 4; /* SD */
        default: return 0;
        }
    }
    return 0;
}

#elif defined(TARGET_M68K)
#include "sst_mmio_m68k.h"
#endif

/*
 * Restore the pre-fault signal mask and resume
 * the guest. We leave via cpu_loop_exit (siglongjmp), which skips the kernel's
 * sigreturn that would otherwise restore the mask — without this the next
 * aperture fault wedges the process. Preserve every originally blocked signal.
 * Does not return.
 */
static G_NORETURN void sst_mmio_resume(CPUState *cpu, const sigset_t *signal_mask)
{
    sigprocmask(SIG_SETMASK, signal_mask, NULL);
    cpu_loop_exit(cpu);
}

/*
 * Called from host_sigsegv_handler. If guest_addr is in an aperture, recover the
 * faulting guest instruction, service it through the mailbox, advance the guest
 * PC and resume via cpu_loop_exit (does not return). Otherwise returns so the
 * normal SEGV path runs.
 */
void sst_mmio_handle_fault(CPUState *cpu, abi_ptr guest_addr, uintptr_t host_pc,
                           bool is_write, const sigset_t *signal_mask)
{
    const struct SstMmioRange *r = find_range(guest_addr);
    if (!r) {
        return;
    }

    /* QEMU plugins use exactly CPUState.cpu_index for this guest thread.
     * Preserve that identity for per-core draining/coherence on the SST side. */
    const unsigned vcpu = cpu->cpu_index;
    if (vcpu >= quetz_ipc_vcpu_count(r->client)) {
        return; /* no mailbox for this guest thread: deliver the guest fault */
    }

    /* Recover guest CPU state (env->pc) at the faulting instruction. */
    if (!cpu_restore_state(cpu, host_pc)) {
        return;
    }

#if defined(TARGET_RISCV64)
    CPURISCVState *env = cpu_env(cpu);
    target_ulong guest_pc = env->pc;

    uint32_t insn = 0;
    if (get_user_u32(insn, guest_pc) != 0) {
        return;
    }
    int is_store = 0, rd = 0, rs2 = 0, len, is_signed = 0;
    unsigned size = 0;
    len = decode_ldst(insn, &is_store, &size, &rd, &rs2, &is_signed);
    if (!len || is_store != is_write || size > r->size - (guest_addr - r->base)) {
        return;
    }

    if (is_store) {
        uint64_t val = env->gpr[rs2];
        quetz_ipc_mmio_write(r->client, vcpu, guest_addr, size, val);
    } else {
        uint64_t val = quetz_ipc_mmio_read(r->client, vcpu,
                                           guest_addr, size);
        if (size < 8) {
            /* The device returns the low `size` bytes; widen to the 64-bit GPR
             * per the load's signedness (sign-extend LB/LH/LW/C.LW, else zero). */
            unsigned shift = 64 - size * 8;
            val = is_signed ? (uint64_t)(((int64_t)(val << shift)) >> shift)
                            : (val & ((1ull << (size * 8)) - 1));
        }
        if (rd != 0) {
            env->gpr[rd] = val;
        }
    }
    env->pc = guest_pc + len;
    sst_mmio_resume(cpu, signal_mask); /* does not return */

#elif defined(TARGET_M68K)
    CPUM68KState *env = cpu_env(cpu);
    target_ulong guest_pc = env->pc;

    uint16_t op = 0;
    if (get_user_u16(op, guest_pc) != 0) {
        return;
    }
    int is_store = 0, dreg = 0, len;
    unsigned size = 0, index_offset = 0;
    len = m68k_mmio_decode(op, &is_store, &size, &dreg, &index_offset);
    if (!len || is_store != is_write || size > r->size - (guest_addr - r->base)) {
        return;
    }

    if (index_offset) {
        uint16_t ext;
        if (get_user_u16(ext, guest_pc + index_offset) != 0 || (ext & 0x0100)) {
            return; /* full indexed EA has variable length; only brief supported */
        }
    }
    uint32_t moved;
    uint32_t mask = (size >= 4) ? 0xFFFFFFFFu : ((1u << (size * 8)) - 1u);
    if (is_store) {
        uint32_t raw;
        if (dreg < 0) {
            /* immediate source: word for .B/.W (byte in low 8), long for .L */
            if (size >= 4) {
                if (get_user_u32(raw, guest_pc + 2) != 0) {
                    return;
                }
            } else {
                uint16_t w = 0;
                if (get_user_u16(w, guest_pc + 2) != 0) {
                    return;
                }
                raw = w;
            }
        } else if (dreg < 8) {
            raw = env->dregs[dreg];
        } else {
            raw = env->aregs[dreg - 8];
        }
        moved = raw & mask;
        quetz_ipc_mmio_write(r->client, vcpu, guest_addr, size, moved);
    } else {
        uint64_t val = quetz_ipc_mmio_read(r->client, vcpu,
                                           guest_addr, size);
        moved = (uint32_t)val & mask;
        if (dreg < 8) {
            env->dregs[dreg] = (env->dregs[dreg] & ~mask) | ((uint32_t)val & mask);
        } else {
            /* MOVEA: .W sign-extends to 32 bits, .L is the full value. */
            env->aregs[dreg - 8] = (size == 2)
                ? (uint32_t)(int32_t)(int16_t)val : (uint32_t)val;
        }
    }
    if (is_store || dreg < 8) {
        cpu_m68k_set_ccr(env, m68k_mmio_move_ccr(moved, size, env->cc_x << 4));
    }
    env->pc = guest_pc + len;
    sst_mmio_resume(cpu, signal_mask); /* does not return */

#else
    (void)host_pc;
#endif
}
