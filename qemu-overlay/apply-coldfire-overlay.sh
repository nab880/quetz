#!/bin/sh
set -eu
QEMU_SRC="${1:-.}"
OVERLAY="$(cd "$(dirname "$0")" && pwd)"
[ -d "$QEMU_SRC/hw/misc" ] || { echo "Not a QEMU source tree: $QEMU_SRC" >&2; exit 1; }
[ "$(cat "$QEMU_SRC/.quetz-generic-overlay" 2>/dev/null || true)" = quetz-generic-v6 ] || {
    echo "Apply the matching generic Quetz overlay first" >&2; exit 1;
}
cp "$OVERLAY/../include/quetz/quetz_coldfire_cache.h" "$QEMU_SRC/include/quetz/"
# Retain BSP RAMBAR writes that stock cfv4e QEMU rejects.
QEMU_SRC="$QEMU_SRC" python3 - <<'PY'
import os

src = os.environ["QEMU_SRC"]

cpu_h = os.path.join(src, "target/m68k/cpu.h")
text = open(cpu_h).read()
if "uint32_t rambar1;" not in text:
    anchor = "    uint32_t rambar0;\n"
    assert text.count(anchor) == 1, (
        "RAMBAR state anchor is missing or ambiguous in target/m68k/cpu.h"
    )
    text = text.replace(anchor, anchor + "    uint32_t rambar1;\n", 1)
    open(cpu_h, "w").write(text)

helper = os.path.join(src, "target/m68k/helper.c")
text = open(helper).read()
marker = "case M68K_CR_RAMBAR1:"
if marker not in text:
    anchor = "    case M68K_CR_VBR:\n        env->vbr = val;\n        break;\n"
    assert text.count(anchor) == 1, (
        "ColdFire MOVEC anchor is missing or ambiguous in target/m68k/helper.c"
    )
    insert = (
        "    case M68K_CR_RAMBAR0:\n"
        "        env->rambar0 = val;\n"
        "        break;\n"
        "    case M68K_CR_RAMBAR1:\n"
        "        env->rambar1 = val;\n"
        "        break;\n"
    )
    text = text.replace(anchor, anchor + insert, 1)
    open(helper, "w").write(text)

cpu_c = os.path.join(src, "target/m68k/cpu.c")
text = open(cpu_c).read()
if "VMSTATE_UINT32_V(env.rambar1" not in text:
    start = text.index("const VMStateDescription vmstate_cf_spregs")
    end = text.index("};", start)
    block = text[start:end]
    block = block.replace(".version_id = 1", ".version_id = 2", 1)
    anchor = "        VMSTATE_UINT32(env.rambar0, M68kCPU),\n"
    assert anchor in block, "RAMBAR migration anchor missing in target/m68k/cpu.c"
    block = block.replace(
        anchor,
        anchor + "        VMSTATE_UINT32_V(env.rambar1, M68kCPU, 2),\n",
        1,
    )
    text = text[:start] + block + text[end:]
    open(cpu_c, "w").write(text)

print("ColdFire RAMBAR overlay applied")
PY
python3 "$OVERLAY/patch-cached-ram.py" "$QEMU_SRC"
# --- hw/m68k/mcf_intc.c: expose the 64 interrupt inputs as qdev GPIOs -------
# The mcf5208evb machine wires its devices through qemu_allocate_irqs() and
# frees the array, so a foreign device (sst-mmio-bridge IRQ injection) has no
# path to the controller inputs. Registering them as qdev GPIO inputs makes
# them addressable via qdev_get_gpio_in(dev, line); mcf_intc_set_irq already
# has the qemu_irq_handler signature and casts its opaque from the device
# pointer, which is what qdev GPIOs pass. Anchor-based + idempotent, like the
# linux-user edits below.
if [ -f "$QEMU_SRC/hw/m68k/mcf_intc.c" ]; then
    QEMU_SRC="$QEMU_SRC" python3 - <<'PY'
import os
src = os.environ["QEMU_SRC"]
p = os.path.join(src, "hw/m68k/mcf_intc.c")
s = open(p).read()
if "qdev_init_gpio_in" not in s:
    anchor = ('    memory_region_init_io(&s->iomem, obj, &mcf_intc_ops, s,'
              ' "mcf", 0x100);\n')
    assert anchor in s, "anchor missing in hw/m68k/mcf_intc.c"
    ins = ("    /* Quetz overlay: expose the 64 interrupt inputs as qdev GPIOs\n"
           "     * so sst-mmio-bridge can inject SST-device IRQs by line. */\n"
           "    qdev_init_gpio_in(DEVICE(obj), mcf_intc_set_irq, 64);\n")
    s = s.replace(anchor, anchor + ins, 1)
    open(p, "w").write(s)
print("mcf_intc qdev GPIO overlay applied")
PY
fi
printf '%s\n' quetz-coldfire-v5 > "$QEMU_SRC/.quetz-coldfire-overlay"
