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



#ifndef SST_QUETZ_IPC_CPP_H
#define SST_QUETZ_IPC_CPP_H
#include <quetz/quetz_ipc_types.h>
namespace SST { namespace Quetz {
using ::QuetzShmemCmd;
using ::QuetzInsnClass;
using ::QuetzCommand;
using ::QuetzMmioResponseSlot;
using ::QuetzMmioSyncRequest;
using ::QuetzIrqSlot;
using ::QuetzSharedData;
using ::QuetzNativeRamRegion;
using ::QUETZ_MAX_NATIVE_REGIONS;
using ::QUETZ_NATIVE_REGION_BYTES;
using ::QUETZ_SHM_MAGIC;
using ::QUETZ_MAX_MMIO_VCORES;
using ::QUETZ_MAX_IRQ_LINES;
using ::QUETZ_COMPUTE_RUN_MAX;
using ::QUETZ_CMD_DATA_BYTES;
using ::QUETZ_CMD_NOP;
using ::QUETZ_CMD_READ;
using ::QUETZ_CMD_WRITE;
using ::QUETZ_CMD_EXIT;
using ::QUETZ_CMD_MMIO_READ_REQ;
using ::QUETZ_CMD_MMIO_WRITE_REQ;
using ::QUETZ_CMD_CACHE_OP;
using ::QUETZ_CMD_COMPUTE_RUN;
using ::QUETZ_INSN_INT_MEM;
using ::QUETZ_INSN_FP_MEM;
using ::QUETZ_INSN_VEC_MEM;
using ::QUETZ_INSN_INT_COMPUTE;
using ::QUETZ_INSN_FP_COMPUTE;
using ::QUETZ_INSN_VEC_COMPUTE;
using ::QUETZ_INSN_BRANCH;
using ::QUETZ_INSN_OTHER;
using ::QUETZ_INSN_CLASS_COUNT;
using ::quetz_native_ram;
using ::quetz_configure_native_regions;
} }
#endif
