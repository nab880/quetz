"""Exercise fatal configuration rejection through the real SST component loader."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sst', required=True)
    parser.add_argument('--library-dir', required=True)
    args = parser.parse_args()
    cases = [
        ('missing_firmware', {}, "No 'executable' parameter provided"),
        ('topology', {
            'executable': '/unused/firmware.elf', 'system_mode': 1,
            'architecture_adapter': 'coldfire-v4e',
            'platform_machine': 'mcf5208evb', 'vcpu_count': 1,
            'qemu_args': '-M mcf5208evb -cpu cfv4e -smp 2 -accel tcg,thread=single',
        }, 'QEMU topology does not match configured CPU count'),
        ('native_region', {
            'executable': '/unused/firmware.elf',
            'cache_native_regions': '0x12000001:0x1000',
        }, 'native regions require 4-KiB alignment'),
    ]
    with tempfile.TemporaryDirectory(prefix='quetz-invalid-config-') as tmp:
        for name, params, diagnostic in cases:
            deck = Path(tmp) / (name + '.py')
            deck.write_text('import sst\ncpu = sst.Component("invalid", "quetz.QuetzComponent")\n'
                            + 'cpu.addParams(' + repr(params) + ')\n')
            result = subprocess.run([args.sst, '--lib-path=' + args.library_dir, str(deck)],
                                    capture_output=True, text=True, timeout=15,
                                    env=os.environ | {'OMPI_ALLOW_RUN_AS_ROOT': '1',
                                                      'OMPI_ALLOW_RUN_AS_ROOT_CONFIRM': '1'})
            output = result.stdout + result.stderr
            if result.returncode <= 0 or result.returncode == 139 or diagnostic not in output:
                raise AssertionError(f'{name}: invalid configuration did not fail cleanly '
                                     f'(exit {result.returncode})\n{output}')
            if 'Segmentation fault' in output or 'SIGSEGV' in output:
                raise AssertionError(f'{name}: crash during fatal configuration rejection\n{output}')
            print(f'PASS {name}: exit {result.returncode}, expected diagnostic, no crash')


if __name__ == '__main__':
    main()
