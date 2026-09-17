#!/bin/sh
set -eu
QEMU_SRC="${1:-.}"
OVERLAY="$(cd "$(dirname "$0")" && pwd)"
[ -d "$QEMU_SRC/hw/misc" ] || { echo "Not a QEMU source tree: $QEMU_SRC" >&2; exit 1; }
cp "$OVERLAY/hw/misc/sst_mmio_bridge.c" "$QEMU_SRC/hw/misc/"
cp "$OVERLAY/quetz_ipc_client.c" "$QEMU_SRC/hw/misc/"
mkdir -p "$QEMU_SRC/include/quetz"
cp "$OVERLAY/../include/quetz/quetz_ipc_types.h" "$QEMU_SRC/include/quetz/"
cp "$OVERLAY/../include/quetz/quetz_ipc_client.h" "$QEMU_SRC/include/quetz/"
cp "$OVERLAY/../include/quetz/quetz_ipc_lock.h" "$QEMU_SRC/include/quetz/"
# Append softmmu source registrations to hw/misc/meson.build.
HW_MESON="$QEMU_SRC/hw/misc/meson.build"
if ! grep -q sst_mmio_bridge.c "$HW_MESON"; then
    cat >> "$HW_MESON" <<'EOF'

# Quetz MMIO bridge (added by sst overlay)
system_ss.add(files('sst_mmio_bridge.c'))
system_ss.add(files('quetz_ipc_client.c'))
EOF
fi
# --- linux-user (P6): SIGSEGV-trap synchronous MMIO --------------------------
# System mode traps the doorbell with the sst-mmio-bridge device; user mode has
# no device map, so qemu-<arch> reserves the aperture PROT_NONE and routes the
# resulting SIGSEGV to the same sync mailbox. Edits are anchor-based + idempotent
# so they tolerate QEMU point releases (developed against 9.2.1).
if [ -d "$QEMU_SRC/linux-user" ]; then
    cp "$OVERLAY/linux-user/sst_mmio.c" "$QEMU_SRC/linux-user/sst_mmio.c"
    cp "$OVERLAY/linux-user/sst_mmio.h" "$QEMU_SRC/linux-user/sst_mmio.h"
    cp "$OVERLAY/linux-user/sst_mmio_m68k.h" "$QEMU_SRC/linux-user/sst_mmio_m68k.h"

    QEMU_SRC="$QEMU_SRC" python3 - <<'PY'
import os
src = os.environ["QEMU_SRC"]
q = chr(39)

def patch(path, edits):
    p = os.path.join(src, path)
    s = open(p).read()
    for marker, anchor, ins, after in edits:
        if marker in s:
            continue
        assert s.count(anchor) == 1, "anchor missing/ambiguous in %s: %r" % (path, anchor)
        s = s.replace(anchor, (anchor + ins) if after else (ins + anchor), 1)
    open(p, "w").write(s)

# linux-user/meson.build: compile sst_mmio.c + ipc client into every target
# (linux_user_ss is built per-target; sst_mmio.c is internally TARGET_*-guarded).
patch("linux-user/meson.build", [(
    "sst_mmio.c",
    "linux_user_ss.add(rt)\n",
    "\n# Quetz user-mode synchronous MMIO (P6)\n"
    "linux_user_ss.add(files(" + q + "sst_mmio.c" + q + "))\n"
    "linux_user_ss.add(files(" + q + "../hw/misc/quetz_ipc_client.c" + q + "))\n",
    True)])

# linux-user/main.c: include, arg handler, arg_table entry, aperture reservation.
patch("linux-user/main.c", [
    ("sst_mmio.h", '#include "qemu.h"\n', '#include "sst_mmio.h"\n', True),
    ("handle_arg_sst_mmio_range",
     "static const struct qemu_argument arg_table[] = {\n",
     "static void handle_arg_sst_mmio_range(const char *arg)\n"
     "{\n    sst_mmio_register_range(arg);\n}\n\n", False),
    ("QEMU_SST_MMIO_RANGE",
     "    {NULL, NULL, false, NULL, NULL, NULL}\n};",
     '    {"sst-mmio-range", "QEMU_SST_MMIO_RANGE", true, handle_arg_sst_mmio_range,\n'
     '     "spec",       "Quetz sync MMIO range shmname=,base=,size= (repeatable)"},\n',
     False),
    ("sst_mmio_apply_reservation", "    cpu_loop(env);\n",
     "    sst_mmio_apply_reservation();\n", False),
])

# Upgrade the earlier three-argument signal hook before the idempotent patch.
p = os.path.join(src, "linux-user/signal.c")
s = open(p).read()
s = s.replace("    sst_mmio_handle_fault(cpu, guest_addr, pc);\n",
    "    if (is_valid && access_type != MMU_INST_FETCH &&\n"
    "        in_code_gen_buffer((void *)(pc - tcg_splitwx_diff))) {\n"
    "        sst_mmio_handle_fault(cpu, guest_addr, pc, is_write, host_signal_mask(uc));\n"
    "    }\n")
open(p, "w").write(s)

# linux-user/signal.c: include + the host_sigsegv_handler hook.
patch("linux-user/signal.c", [
    ("sst_mmio.h", '#include "host-signal.h"\n', '#include "sst_mmio.h"\n', True),
    ("sst_mmio_handle_fault",
     "    MMUAccessType access_type = adjust_signal_pc(&pc, is_write);\n"
     "    bool maperr;\n",
     "\n    /* Quetz P6: route reserved-aperture faults to the sync mailbox.\n"
     "     * On a match this does not return (cpu_loop_exit). */\n"
     "    if (is_valid && access_type != MMU_INST_FETCH &&\n"
     "        in_code_gen_buffer((void *)(pc - tcg_splitwx_diff))) {\n"
     "        sst_mmio_handle_fault(cpu, guest_addr, pc, is_write, host_signal_mask(uc));\n"
     "    }\n", True),
])
print("linux-user overlay applied")
PY
fi

echo "Quetz QEMU overlay applied under $QEMU_SRC"

printf '%s\n' quetz-generic-v6 > "$QEMU_SRC/.quetz-generic-overlay"
