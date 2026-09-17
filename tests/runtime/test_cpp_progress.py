"""Compile production control flow with lightweight SST transport callbacks.

Only includes/SST registration plumbing are replaced. The methods under test
are read from the current source tree, so this cannot silently test old copies
of the scheduling and DMA algorithms. Full installed-SST tests remain separate.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def source(name):
    return re.sub(r'^#include[^\n]*\n', '', (ROOT / 'src' / name).read_text(), flags=re.M)


def method(name, signature):
    text = (ROOT / 'src' / name).read_text()
    start = text.rfind('\n', 0, text.index(signature)) + 1
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'


class ProductionProgressTests(unittest.TestCase):
    def test_runtime_progress(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('C++ compiler needed for runtime source regressions')
        text = (HERE / 'progress_stubs.h').read_text()
        text += source('quetz_stats.h')
        fields = re.findall(r'Statistic<uint64_t>\*\s*(\w+)', source('quetz_stats.h'))
        text += 'struct Stats: SST::Quetz::QuetzCoreStats {\n'
        text += f'SST::Statistics::Statistic<uint64_t> counters[{len(fields)}];\nStats() {{\n'
        text += ''.join(f'{name} = &counters[{i}];\n' for i, name in enumerate(fields))
        text += '}};\n'
        for name in ('quetz_mem_issue.h', 'quetz_mem_issue.cc', 'quetz_pipeline_api.h',
                     'quetz_pipeline.h', 'quetz_pipeline_input.cc', 'quetz_pipeline_transform.cc',
                     'quetz_pipeline_output.cc', 'quetz_pipeline.cc',
                     'quetz_balar_flush_range.h', 'quetz_accelerator_port.h',
                     'quetz_balar_accelerator_port.h', 'quetz_balar_accelerator_port.cc'):
            text += source(name)
        text += '''
struct QuetzGpuDevice {
 uint64_t dma_range_start_=0x90000000,dma_range_end_=0x9000ffff;
 struct Args { uint64_t src_addr,dst_addr; } op_args_;
 uint64_t op_next_dma_off_=0,op_in_bytes_=128,op_dma_outstanding_=0;
 static constexpr uint64_t kOpDmaChunk=64,kMaxOpDmaOutstanding=64;
 std::unordered_map<uint64_t,uint64_t> op_req_off_;
 std::vector<uint8_t> op_out_;
 StandardMem* mem_iface_;
 bool dmaRangeOk(uint64_t,uint64_t)const;
 void opIssueReadWindow(); void opIssueWriteWindow();
};
'''
        for name in ('dmaRangeOk', 'opIssueReadWindow', 'opIssueWriteWindow'):
            text += method('quetz_gpu_device.cc', 'QuetzGpuDevice::' + name)
        text += '''
struct QemuLauncher {
 SST::Output* output_; pid_t pid_=0;
 explicit QemuLauncher(SST::Output* out): output_(out) {}
 bool checkChild();
 void terminate(bool expect_guest_exit);
};
'''
        text += method('quetz_launcher.cc', 'QemuLauncher::checkChild')
        text += method('quetz_launcher.cc', 'static bool reapBounded')
        text += method('quetz_launcher.cc', 'QemuLauncher::terminate')
        text += (HERE / 'cpu_tick_cases.h').read_text()
        text += method('quetzcpu.cc', 'bool QuetzCPU::tick(')
        text += (HERE / 'progress_cases.h').read_text()
        with tempfile.TemporaryDirectory(prefix='quetz-progress-') as tmp:
            src, binary = Path(tmp) / 'progress.cc', Path(tmp) / 'progress'
            src.write_text(text)
            built = subprocess.run([compiler, '-std=c++17', '-O0', '-g', '-I', str(ROOT / 'src'),
                                    '-I', str(ROOT / 'include'), str(src), '-o', str(binary)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True,
                                    text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS production pipeline', result.stdout)


if __name__ == '__main__':
    unittest.main()
