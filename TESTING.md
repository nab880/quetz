# Tests

Run the standalone model and ABI tests without SST or QEMU:

```sh
cmake -S . -B build-models -DQUETZ_BUILD_RUNTIME=OFF
cmake --build build-models --parallel 2
ctest --test-dir build-models --output-on-failure
```

When SST headers are installed, pass `-DSST_ROOT=/opt/sst` to include the platform-profile configuration test. On Linux this also builds the actual QEMU C IPC client against an SST tunnel and verifies attachment, native RAM sharing, reset epochs, competing-client mailbox ownership, per-vCPU concurrent reads/writes and rejection of mismatched ABI magic. The suite covers cache behavior, software-managed multicore visibility, native descriptors, independent C/C++ ABI layout, distinct shared-memory mapping residues, cache-operation decoders, FFT, event writing and launcher constraints. The Python cache-policy test compares 16,384 native QEMU routing decisions with the SST model. Runner tests use mocked subprocesses.

A full runtime proof also requires loading the installed element and running real QEMU:

```sh
runtime/build.sh
runtime/quetz-run --out artifacts/hello
```

The runner writes `sst.log`, guest `uart.txt` and `result.txt`; PASS requires both a successful simulator exit and the TestFinisher PASS sentinel. Timeout, missing sentinel and explicit failure remain failures. Set `--deck` for another deck and `--firmware` for an explicit ELF. Custom decks must bring their own adjacent Python helpers and assets.

`tests/testsuite_default_quetz.py` and `tests/expanded_coldfire_tests.py` contain the broader SST test-framework integration suites. They require the corresponding cross-compiled fixtures and QEMU targets. Platform board, boot, recovery and application acceptance tests are maintained by each platform repository.

The GitHub Actions workflow builds the model tests and a complete standalone Docker runtime, then runs the stock ColdFire example. It checks out only this repository; no platform sources or board configuration are required.

For the Linux user-mode MMIO integration regressions, build QEMU with
`m68k-linux-user,riscv64-linux-user` in `QEMU_TARGET_LIST`, install
`gcc-m68k-linux-gnu` and `gcc-riscv64-linux-gnu`, then run:

```sh
QEMU_PREFIX=/opt/qemu SST_HOME=/opt/sst tests/usermode/run_mmio_regressions.sh
```

This uses a real SST shared-memory tunnel. The m68k guest branches immediately
after byte, word and long MMIO reads and writes, checking N/Z/V/C and preserved
X, and checks every CCR value across MOVEA. Four concurrent RISC-V guest threads
receive CPU-specific replies and verify that requests retain their vCPU identity.
Each process has a timeout; missing transactions or a failed guest fail the test.
