// Copyright 2009-2026 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2026, NTESS
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.



#ifndef QUETZ_IPC_TYPES_H
#define QUETZ_IPC_TYPES_H
#include <stddef.h>
#include <stdint.h>

typedef enum QuetzShmemCmd {
    QUETZ_CMD_NOP = 0, QUETZ_CMD_READ = 1, QUETZ_CMD_WRITE = 2,
    QUETZ_CMD_EXIT = 3, QUETZ_CMD_MMIO_READ_REQ = 4,
    QUETZ_CMD_MMIO_WRITE_REQ = 5, QUETZ_CMD_COMPUTE_RUN = 6,
    QUETZ_CMD_CACHE_OP = 7
} QuetzShmemCmd;

typedef enum QuetzInsnClass {
    QUETZ_INSN_INT_MEM = 0, QUETZ_INSN_FP_MEM = 1, QUETZ_INSN_VEC_MEM = 2,
    QUETZ_INSN_INT_COMPUTE = 3, QUETZ_INSN_FP_COMPUTE = 4,
    QUETZ_INSN_VEC_COMPUTE = 5, QUETZ_INSN_BRANCH = 6, QUETZ_INSN_OTHER = 7,
    QUETZ_INSN_CLASS_COUNT = 8
} QuetzInsnClass;

enum {
    QUETZ_COMPUTE_RUN_MAX = 4096, QUETZ_CMD_DATA_BYTES = 64,
    QUETZ_MAX_MMIO_VCORES = 256, QUETZ_MAX_IRQ_LINES = 64,
    QUETZ_SHM_MAGIC = 0x515A4D06u,
    QUETZ_MAX_NATIVE_REGIONS = 2, QUETZ_NATIVE_REGION_BYTES = 65536
};

typedef struct QuetzCommand {
    QuetzShmemCmd cmd;
    uint32_t size;
    uint64_t pc, addr;
    uint32_t insn_class, _pad;
    uint8_t data[QUETZ_CMD_DATA_BYTES];
} QuetzCommand;

typedef struct QuetzMmioResponseSlot {
    volatile uint32_t ready;
    uint32_t _pad;
    uint64_t value;
} QuetzMmioResponseSlot;

typedef struct QuetzMmioSyncRequest {
    volatile uint32_t pending;
    uint32_t cmd, size;
    /* Producer ownership spans request publication through response consumption. */
    uint32_t busy;
    uint64_t addr, write_val;
} QuetzMmioSyncRequest;

/* Single-writer release/acquire level mailbox; pulses between polls are not retained. */
typedef struct QuetzIrqSlot {
    volatile uint32_t seq;
    uint32_t level;
} QuetzIrqSlot;

typedef struct QuetzNativeRamRegion {
    uint32_t base, size, offset, reserved;
} QuetzNativeRamRegion;

typedef struct QuetzSharedData {
    size_t numCores;
    uint64_t simTime, simCycles;
    volatile uint32_t child_attached;
    uint32_t _pad0;
    QuetzMmioResponseSlot mmio_slot[QUETZ_MAX_MMIO_VCORES];
    QuetzMmioSyncRequest mmio_req[QUETZ_MAX_MMIO_VCORES];
    QuetzIrqSlot irq_slot[QUETZ_MAX_MMIO_VCORES][QUETZ_MAX_IRQ_LINES];
    volatile uint32_t irq_generation;
    uint32_t _pad2;
    volatile uint32_t cpu_reset_epoch[QUETZ_MAX_MMIO_VCORES];
    uint32_t native_region_count;
    QuetzNativeRamRegion native_regions[QUETZ_MAX_NATIVE_REGIONS];
    uint8_t native_ram_storage[(QUETZ_MAX_NATIVE_REGIONS + 1) * QUETZ_NATIVE_REGION_BYTES - 1];
    volatile uint32_t magic; /* Keep last: size/layout skew must fail attach. */
    uint32_t _pad1;
} QuetzSharedData;

static inline int quetz_native_regions_valid(const QuetzSharedData *shared)
{
    const size_t begin = offsetof(QuetzSharedData, native_ram_storage);
    const size_t end = begin + sizeof(shared->native_ram_storage);
    if (shared->native_region_count > QUETZ_MAX_NATIVE_REGIONS) return 0;
    for (unsigned i = 0; i < shared->native_region_count; ++i) {
        const QuetzNativeRamRegion *a = &shared->native_regions[i];
        if (!a->size || a->size > QUETZ_NATIVE_REGION_BYTES ||
            (a->base & 4095u) || (a->size & 4095u) ||
            (uint64_t)a->base + a->size > (UINT64_C(1) << 32) ||
            a->offset < begin || a->offset > end - a->size || a->reserved ||
            (((uintptr_t)shared + a->offset) & 4095u)) return 0;
        for (unsigned j = 0; j < i; ++j) {
            const QuetzNativeRamRegion *b = &shared->native_regions[j];
            if (((uint64_t)a->base < (uint64_t)b->base + b->size &&
                 (uint64_t)b->base < (uint64_t)a->base + a->size) ||
                (a->offset < b->offset + b->size && b->offset < a->offset + a->size)) return 0;
        }
    }
    return 1;
}

static inline uint8_t *quetz_native_ram(QuetzSharedData *shared, unsigned region)
{
    if (region >= shared->native_region_count || !quetz_native_regions_valid(shared)) return NULL;
    return (uint8_t *)shared + shared->native_regions[region].offset;
}

static inline int quetz_configure_native_regions(QuetzSharedData *shared,
                                                const QuetzNativeRamRegion *regions,
                                                unsigned count)
{
    if (count > QUETZ_MAX_NATIVE_REGIONS || (count && !regions)) return 0;
    const uintptr_t first = ((uintptr_t)shared->native_ram_storage +
        QUETZ_NATIVE_REGION_BYTES - 1) & ~(uintptr_t)(QUETZ_NATIVE_REGION_BYTES - 1);
    shared->native_region_count = count;
    for (unsigned i = 0; i < count; ++i) {
        shared->native_regions[i] = regions[i];
        /* One shared-relative offset; clients must never realign their own view. */
        shared->native_regions[i].offset = (uint32_t)(first - (uintptr_t)shared) + i * QUETZ_NATIVE_REGION_BYTES;
        shared->native_regions[i].reserved = 0;
    }
    if (quetz_native_regions_valid(shared)) return 1;
    shared->native_region_count = 0;
    return 0;
}
#endif
