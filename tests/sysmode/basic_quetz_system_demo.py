"""Stock QEMU ColdFire board smoke test, independent of platform repositories."""
import os
from pathlib import Path
import runpy
import shutil
here = Path(__file__).resolve().parent
for key, value in {
    "QUETZ_EXE": str(here / "firmware/coldfire_hello"),
    "QUETZ_QEMU": shutil.which("qemu-system-m68k") or "/opt/qemu/bin/qemu-system-m68k",
    "QUETZ_QEMU_ARGS": "-M mcf5208evb -nographic -m 128M",
    "QUETZ_RAM_START": "0x40000000", "QUETZ_RAM_END": "0x47ffffff",
    "QUETZ_REGION_HANDLER_COUNT": "2",
    "QUETZ_REGION_HANDLER0_TYPE": "filtered",
    "QUETZ_REGION_HANDLER0_START": "0xfc000000", "QUETZ_REGION_HANDLER0_END": "0xfcffffff",
    "QUETZ_REGION_HANDLER1_TYPE": "testfinish",
    "QUETZ_REGION_HANDLER1_START": "0x80000000", "QUETZ_REGION_HANDLER1_END": "0x80000003",
    "QUETZ_REGION_HANDLER1_PASS_VALUE": "21845",
}.items():
    os.environ.setdefault(key, value)
runpy.run_path(str(here / "basic_quetz_sysmode.py"), run_name="__main__")
