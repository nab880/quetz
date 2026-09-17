#ifndef SST_MMIO_H
#define SST_MMIO_H

#include "qemu/osdep.h"
#include "exec/cpu_ldst.h"
#include "hw/core/cpu.h"
#include "quetz/quetz_ipc_client.h"

struct SstMmioRange {
    char     shmname[256];
    uint64_t base;
    uint64_t size;
    unsigned vcpu_id;
    QuetzIpcClient *client;
};

/* Parse one -sst-mmio-range SPEC (shmname=,base=,size=). Repeatable.
 * The faulting CPU index selects the mailbox; legacy vcpu_id=0 is accepted. */
void sst_mmio_register_range(const char *spec);

/* Reserve registered apertures as PROT_NONE; call once after guest_base setup. */
void sst_mmio_apply_reservation(void);

/* SIGSEGV hook: services an aperture fault and resumes (no return), else returns. */
void sst_mmio_handle_fault(CPUState *cpu, abi_ptr guest_addr, uintptr_t host_pc,
                           bool is_write, const sigset_t *signal_mask);

#endif
