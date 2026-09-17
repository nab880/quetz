# Configure a system

An SST deck instantiates `quetz.QuetzComponent`, supplies a QEMU binary, plugin and firmware, connects its `cache_link_N` ports to memory, and attaches region handlers or accelerator subcomponents. Start with `tests/sysmode/basic_quetz_system_demo.py` and `tests/sysmode/basic_quetz_sysmode.py`. The former selects a stock ColdFire machine; the latter accepts environment configuration for reusable tests.

The platform boundary is explicit:

| SST parameter | Meaning |
| --- | --- |
| `architecture_adapter` | Empty by default; `coldfire-v4e` enables the bounded ColdFire contract. |
| `platform_machine` | Expected QEMU machine name, supplied by the platform. |
| `cache_native_regions` | Ordered `base:size` pairs separated by semicolons; empty by default. |

Native ranges must have 4-KiB aligned bases and sizes, fit within 32-bit guest addresses, be disjoint, and contain at most two regions of at most 64 KiB each. They require `sst_window_cache=1`. The SST window and native ranges must not overlap. For two CPUs, pass `vcpu_count=2`, `-smp 2` and `-accel tcg,thread=single`. Platform code validates board properties and secondary firmware. Quetz validates architecture, topology and transport limits.

`include/quetz/quetz_ipc_types.h` is the single C/C++ ABI definition. ABI v6 uses magic `0x515A4D06` and serializes concurrent mailbox producers until each response is consumed; native RAM descriptors contain guest base, size and a shared-relative backing offset. The allocating process sets offsets once. QEMU maps the same offsets even when its host mapping has a different alignment residue. The mailbox layout, reset epochs, event and result semantics remain shared across both sides.

QEMU boards use `quetz_ipc_native_region_count`, `quetz_ipc_native_region` and `quetz_ipc_native_ram` from `quetz_ipc_client.h`. The backing lookup requires an exact base/size match. ColdFire boards include `quetz_coldfire_cache.h` after the target CPU header and call `quetz_coldfire_configure_cache` for each CPU before execution. CPU wiring survives reset; architectural cache control state resets normally.

The generic launcher creates the synchronous bridge from `QUETZ_MMIO_START/END` and optional `QUETZ_SST_WIN_START/END`. IRQ delivery requires `QUETZ_IRQ_LINES` and an explicit `QUETZ_IRQ_INTC_TYPE`; ColdFire examples select `mcf-intc`. Board devices, BSP compatibility options and profiles belong in the consuming platform's launcher or deck.

Accelerator APIs live in `src/quetz_accelerator_port.h`, `src/quetz_kernel_api.h` and `src/quetz_pipeline_api.h`. Installed compatibility headers are under `include/sst/elements/quetz/`. Reusable FFT and scale kernels, streams, sinks and event writing remain in Quetz. Application workloads and production pass/fail policy belong to platform repositories.

User-mode synchronous MMIO routes each fault through the faulting QEMU CPU index,
the same index used by the trace plugin. Configure enough `vcpu_count` mailboxes
for all guest threads; an access by an unconfigured thread faults instead of
being redirected to CPU 0. Concurrent producers sharing an IPC slot hold its
shared ownership lock through response consumption. Legacy `vcpu_id=0` aperture
arguments remain accepted; nonzero fixed routing is rejected. m68k MOVE updates
N/Z/V/C and preserves X, while MOVEA leaves all condition codes unchanged.
