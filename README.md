# Quetz

Quetz connects QEMU execution to SST timing, memory, MMIO, interrupts and accelerator components. It builds independently against an installed SST core. Example memory systems also use the SST Elements `memHierarchy` library.

```sh
runtime/build.sh
runtime/quetz-run --out artifacts/hello
```

The default example runs `coldfire_hello` on QEMU's stock `mcf5208evb` machine and requires a PASS sentinel. See [build instructions](GETTING-STARTED.md), [system configuration](SIMULATING-YOUR-SYSTEM.md) and [tests](TESTING.md).

Quetz owns the SST element, QEMU plugin, transport ABI, generic QEMU overlay and optional ColdFire adapter. Platform repositories own board machines, register maps, device implementations, BSP profiles, firmware and acceptance tests. They consume the public headers in `include/quetz/` and compose their QEMU overlay after Quetz's overlays.

The ColdFire cache adapter currently supports supervisor firmware, one or two `cfv4e` CPUs under single-thread TCG, and at most two native RAM regions of 64 KiB each. These are implementation limits, not claims of complete hardware fidelity. Cache maintenance, private caches and software-managed visibility are modeled; unrestricted multicore, address translation and full peripheral fidelity are outside this adapter's scope.

Component names such as `quetz.QuetzComponent`, accelerator ports and kernel subcomponents remain stable. Source history was extracted from the Quetz element in SST Elements. Existing copyright notices and [SST license terms](LICENSE.md) are retained.
