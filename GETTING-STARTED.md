# Build and run

The runtime image pins SST core, the stock SST Elements dependency and QEMU in `runtime/Dockerfile`. `runtime/fetch-qemu.sh` verifies the QEMU archive checksum before use. Docker builds need network access and a C++ toolchain inside the build image.

```sh
runtime/build.sh
runtime/quetz-run --out artifacts/hello
```

For an existing Linux installation, provide SST core, CMake, a C++17 compiler, pkg-config, GLib development headers and the matching QEMU plugin header:

```sh
cmake -S . -B build -DSST_ROOT=/opt/sst \
  -DQEMU_PLUGIN_INCLUDE_DIR=/opt/qemu/include -DCMAKE_INSTALL_PREFIX=/opt/quetz
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --install build
/opt/quetz/bin/quetz-register
```

The install contains `lib/sst-elements-library/libquetz.so`, `libexec/libqemu_sst_plugin.so`, `include/quetz/` and `share/quetz/tests/`. Set `QUETZ_PLUGIN` to the installed plugin when its prefix differs from SST's. Set `SST_REGISTER` to select a different registration tool. Registration updates the selected SST installation registry when it is writable. For isolated runs, skip registration and set `SST_LIB_PATH=/opt/quetz/lib/sst-elements-library` instead.

Apply overlays to pristine pinned QEMU sources, in order:

```sh
sh qemu-overlay/apply-qemu-overlay.sh /path/to/qemu-9.2.1
sh qemu-overlay/apply-coldfire-overlay.sh /path/to/qemu-9.2.1
# Then apply the chosen platform repository's board overlay, if needed.
```

The second layer is optional for platforms that do not use ColdFire. It retains ColdFire control state, exposes interrupt inputs and adds descriptor-based native RAM routing. The generic layer owns IPC and MMIO delivery. Runtime consumers must rebuild the element, plugin and QEMU together after an ABI change.

The default demo needs `memHierarchy` and a cross-compiled `tests/sysmode/firmware/coldfire_hello`. The Docker build prepares both. See `tests/sysmode/firmware/build.sh` for local fixture builds.
