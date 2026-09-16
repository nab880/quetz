"""Keep native QEMU routing consistent with the SST data-cache policy."""
import ast
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


QUETZ = Path(__file__).resolve().parents[2]


class CachePolicyTests(unittest.TestCase):
    def test_native_router_matches_supervisor_cache_policy(self):
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        if not compiler:
            self.skipTest("a C++17 compiler is required")
        # Read the embedded C without executing the QEMU source patcher.
        tree = ast.parse((QUETZ / "qemu-overlay/patch-cached-ram.py").read_text())
        mode = next(ast.literal_eval(node.value) for node in tree.body
                    if isinstance(node, ast.Assign) and any(
                        isinstance(target, ast.Name) and target.id == "mode"
                        for target in node.targets))
        preamble = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include "quetz_window_cache.h"
#define SR_S 0x2000
struct CPUM68KState { uint32_t cacr, quetz_acr[4], sr; };
'''
        main = r'''
int main() {
    using SST::Quetz::WindowDataCache;
    uint32_t seed = 0x4cf5485;
    auto random = [&] { return seed = seed * 1664525u + 1013904223u; };
    for (unsigned i = 0; i < 16384; ++i) {
        CPUM68KState env{};
        env.sr = SR_S;
        env.cacr = random() & 0x9f000000u;
        env.quetz_acr[0] = random() & ~4u;
        env.quetz_acr[1] = random() & ~4u;
        uint32_t address = random();
        // Include frequent matches to both ACRs, including 1-MiB AMM regions.
        if (i & 1) address = (address & 0x00ffffffu) | (env.quetz_acr[0] & 0xff000000u);
        if (i & 2) address = (address & 0xff0fffffu) | (env.quetz_acr[0] & 0x00f00000u);
        if ((i & 7) == 0) address = (address & 0x00ffffffu) | (env.quetz_acr[1] & 0xff000000u);
        WindowDataCache cache;
        cache.configure(address & ~15u, 16);
        cache.movec(2, env.cacr); cache.next();
        cache.movec(4, env.quetz_acr[0]); cache.next();
        cache.movec(5, env.quetz_acr[1]); cache.next();
        cache.access(address, 1, false);
        const auto action = cache.next();
        const bool cacheable = quetz_native_data_cacheable(&env, address);
        assert(action.kind == WindowDataCache::Kind::Read);
        assert(action.size == (cacheable ? 16u : 1u));
        assert(action.address == (cacheable ? address & ~15u : address));
    }
    std::cout << "16384 supervisor cache routing comparisons passed\n";
}
'''
        with tempfile.TemporaryDirectory(prefix="quetz-cache-policy-") as directory:
            source, executable = Path(directory) / "policy.cc", Path(directory) / "policy"
            source.write_text(preamble + mode + main)
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(QUETZ / "src"), str(source), "-o", str(executable)],
                           check=True, capture_output=True, text=True, timeout=60)
            result = subprocess.run([str(executable)], check=True, capture_output=True,
                                    text=True, timeout=30)
            self.assertEqual(result.stdout, "16384 supervisor cache routing comparisons passed\n")


if __name__ == "__main__":
    unittest.main()
