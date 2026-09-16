# ColdFire examples

Run `runtime/quetz-run` from the repository root for the stock `mcf5208evb` smoke example. It runs `firmware/coldfire_hello`, prints a UART greeting and writes the TestFinisher PASS sentinel.

`basic_quetz_coldfire_system.py` adds reusable stream, sink and accelerator devices. Its interrupt example explicitly selects QEMU's `mcf-intc` controller. Firmware sources and cross-compiler build commands are in `firmware/`.

See [system configuration](../../SIMULATING-YOUR-SYSTEM.md) for the optional `coldfire-v4e` architecture adapter and cache limits. Board-specific machines, BSP support and application acceptance tests are supplied by platform repositories.
