"""Run a real ColdFire line-crossing load/store through the one-slot pipeline."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sst', 'library-dir', 'plugin', 'qemu', 'cc'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    firmware = Path(__file__).resolve().parents[1] / 'sysmode' / 'firmware'
    with tempfile.TemporaryDirectory(prefix='quetz-pipeline-runtime-') as tmp:
        tmp = Path(tmp)
        guest, elf, deck = tmp / 'split.c', tmp / 'split.elf', tmp / 'split.py'
        guest.write_text('''
#include "coldfire_uart.h"
void kernel_main(void) {
    unsigned long value = 0x12345678;
    // Even-aligned longwords are legal on ColdFire and cross a 64-byte line.
    __asm__ volatile("move.l %0,0x4001003e\\n\\t"
                     "move.l 0x4001003e,%0" : "+d"(value) : : "memory");
    testdev_done(value == 0x12345678 ? TESTDEV_PASS : TESTDEV_FAIL);
}
''')
        subprocess.run([args.cc, '-mcpu=5208', '-nostdlib', '-nostartfiles', '-ffreestanding',
                        '-fno-pie', '-no-pie', '-static', '-O1', '-Wl,--build-id=none',
                        '-T', str(firmware / 'link_m68k.ld'), '-I', str(firmware),
                        str(firmware / 'coldfire_startup.S'), str(guest), '-o', str(elf)],
                       check=True, text=True, timeout=30)
        params = {
            'clock': '1GHz', 'vcpu_count': 1, 'maxcorequeue': 2,
            'maxtranscore': 1, 'maxissuepercycle': 1, 'cachelinesize': 64,
            'system_mode': 1, 'qemu': args.qemu, 'qemu_plugin': args.plugin,
            'executable': str(elf), 'qemu_args': '-M mcf5208evb -nographic -m 128M',
        }
        deck.write_text('''import sst
cpu = sst.Component("cpu", "quetz.QuetzComponent")
cpu.addParams(''' + repr(params) + ''')
end = cpu.setSubComponent("region_handler", "quetz.TestFinisherRegionHandler", 0)
end.addParams({"start": 0x80000000, "end": 0x80000003, "pass_value": 0x5555})
memory = sst.Component("memory", "memHierarchy.MemController")
memory.addParams({"clock": "1GHz", "addr_range_start": 0x40000000, "addr_range_end": 0x47ffffff})
backend = memory.setSubComponent("backend", "memHierarchy.simpleMem")
backend.addParams({"access_time": "100ns", "mem_size": "128MiB"})
sst.Link("memory_link").connect((cpu,"cache_link_0","1ns"),(memory,"highlink","1ns"))
# A valid period-form GPU clock must not be rounded down to zero seconds.
gpu = sst.Component("gpu", "quetz.QuetzGpuDevice")
gpu.addParams({"clock": "1ns", "base_addr": 0x90000000})
gpu_iface = gpu.setSubComponent("iface", "memHierarchy.standardInterface")
gpu_memory = sst.Component("gpu_memory", "memHierarchy.MemController")
gpu_memory.addParams({"clock": "1GHz", "addr_range_start": 0, "addr_range_end": 4095})
gpu_backend = gpu_memory.setSubComponent("backend", "memHierarchy.simpleMem")
gpu_backend.addParams({"access_time": "100ns", "mem_size": "4KiB"})
sst.Link("gpu_link").connect((gpu_iface,"lowlink","1ns"),(gpu_memory,"highlink","1ns"))
cpu.enableAllStatistics()
sst.setStatisticLoadLevel(4)
sst.setStatisticOutput("sst.statOutputConsole")
''')
        installed = Path(args.sst).resolve().parents[1] / 'lib' / 'sst-elements-library'
        libraries = args.library_dir + os.pathsep + str(installed)
        result = subprocess.run([args.sst, '--lib-path=' + libraries, str(deck)],
                                capture_output=True, text=True, timeout=30,
                                env=os.environ | {'OMPI_ALLOW_RUN_AS_ROOT': '1',
                                                  'OMPI_ALLOW_RUN_AS_ROOT_CONFIRM': '1'})
        output = result.stdout + result.stderr
        if result.returncode:
            raise AssertionError(f'Pipeline regression failed (exit {result.returncode})\n{output}')
        for name in ('split_read_requests', 'split_write_requests'):
            lines = [line for line in output.splitlines() if name in line]
            if not any(re.search(r'Sum\.u64\s*=\s*[1-9][0-9]*', line) for line in lines):
                raise AssertionError(f'Missing actual {name} observation\n{output}')
        if 'PASS' not in output:
            raise AssertionError('Guest did not signal PASS\n' + output)
        print('PASS real ColdFire/SST load+store split with issue=1, pending=1, queue=2')


if __name__ == '__main__':
    main()
