#!/usr/bin/env bash
# Run in a Linux build environment with installed SST headers and cross compilers.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -I"${SST_HOME:-/opt/sst}/include" -I"$root/include" \
    "$root/tests/usermode/mmio_test_peer.cc" -o "$build/peer"
"${M68K_CC:-m68k-linux-gnu-gcc}" -static -nostdlib -Wl,-e,_start \
    "$root/tests/usermode/sources/m68k_mmio_flags.S" -o "$build/m68k-flags"
"${RV64_CC:-riscv64-linux-gnu-gcc}" -static -nostdlib -march=rv64g -mabi=lp64d -Wl,-e,_start \
    "$root/tests/usermode/sources/rv64_mmio_threads.S" -o "$build/rv64-threads"
"$build/peer" m68k "${QEMU_PREFIX:-/opt/qemu}/bin/qemu-m68k" "$build/m68k-flags"
"$build/peer" riscv64 "${QEMU_PREFIX:-/opt/qemu}/bin/qemu-riscv64" "$build/rv64-threads"
