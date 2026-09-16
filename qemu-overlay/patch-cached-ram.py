"""Apply optional ColdFire data-cache routing to registered native regions."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
cpu_header = (root / "target/m68k/cpu.h").read_text()
if "bool quetz_cache_ram;" in cpu_header and "quetz_cache_region_base[2]" not in cpu_header:
    raise SystemExit("old cache overlay present; use pristine pinned QEMU sources")


def replace(path, marker, anchor, replacement, *, after=None):
    text = path.read_text()
    if marker in text:
        return
    prefix = ""
    if after is not None:
        position = text.index(after)
        prefix, text = text[:position], text[position:]
    if text.count(anchor) != 1:
        raise SystemExit(f"cached RAM overlay anchor missing/ambiguous: {path}: {anchor!r}")
    path.write_text(prefix + text.replace(anchor, replacement, 1))


replace(root / "target/m68k/cpu.h", "uint32_t quetz_acr[4];",
        "    uint32_t cacr;\n",
        "    uint32_t cacr;\n    uint32_t quetz_acr[4];\n")
replace(root / "target/m68k/cpu.h", "uint32_t quetz_cache_region_base[2];",
        "    struct {} end_reset_fields;\n",
        "    struct {} end_reset_fields;\n"
        "    /* Board wiring survives architectural CPU reset. */\n"
        "    bool quetz_cache_ram;\n"
        "    uint32_t quetz_cache_region_count;\n"
        "    uint32_t quetz_cache_region_base[2];\n"
        "    uint32_t quetz_cache_region_size[2];\n")

helper = root / "target/m68k/helper.c"
replace(helper, "Quetz cacheability changes invalidate native data TLB entries",
        "        env->cacr = val;\n        m68k_switch_sp(env);\n",
        "        env->cacr = val;\n"
        "#ifndef CONFIG_USER_ONLY\n"
        "        /* Quetz cacheability changes invalidate native data TLB entries. */\n"
        "        if (env->quetz_cache_ram) tlb_flush(env_cpu(env));\n"
        "#endif\n"
        "        m68k_switch_sp(env);\n")
replace(helper, "env->quetz_acr[reg - M68K_CR_ACR0] = val;",
        "        /* TODO: Implement Access Control Registers.  */\n        break;\n",
        "        env->quetz_acr[reg - M68K_CR_ACR0] = val;\n"
        "#ifndef CONFIG_USER_ONLY\n"
        "        if (env->quetz_cache_ram) tlb_flush(env_cpu(env));\n"
        "#endif\n"
        "        break;\n")

mode = """/* V4e supervisor/user ACR selection, matching the SST cache policy. */
static bool quetz_native_data_cacheable(CPUM68KState *env, uint32_t address)
{
    if (!(env->cacr & 0x80000000u)) return false;
    const bool supervisor = (env->sr & SR_S) != 0;
    for (unsigned i = 0; i < 2; ++i) {
        const uint32_t acr = env->quetz_acr[i];
        const unsigned sm = (acr >> 13) & 3;
        if (!(acr & 0x8000u) || (sm < 2 && sm != supervisor)) continue;
        if (acr & 0x400u) { /* AMM: exact top byte, masked 1-MiB subregion. */
            if ((address >> 24) == (acr >> 24) &&
                ((((address >> 20) ^ (acr >> 20)) & ~(acr >> 16) & 15) == 0))
                return ((acr >> 5) & 3) < 2;
            continue;
        }
        const uint32_t mask = (acr >> 16) & 0xff;
        if ((((address >> 24) ^ (acr >> 24)) & ~mask & 0xff) == 0)
            return ((acr >> 5) & 3) < 2;
    }
    return ((env->cacr >> 25) & 3) < 2;
}

"""
regions = """static bool quetz_native_region_contains(CPUM68KState *env, uint32_t address)
{
    for (unsigned i = 0; i < env->quetz_cache_region_count; ++i)
        if (address >= env->quetz_cache_region_base[i] &&
            address - env->quetz_cache_region_base[i] < env->quetz_cache_region_size[i])
            return true;
    return false;
}

"""
replace(helper, "static bool quetz_native_data_cacheable",
        "bool m68k_cpu_tlb_fill(CPUState *cs, vaddr address, int size,\n",
        mode + regions + "bool m68k_cpu_tlb_fill(CPUState *cs, vaddr address, int size,\n")

routing = """    /* Quetz native RAM data route: never redirect instruction fetch.
     * Separate permissions force a refill if code and data share a TLB page. */
    if (env->quetz_cache_ram &&
        quetz_native_region_contains(env, address) &&
        quetz_native_data_cacheable(env, address)) {
        if (env->mmu.tcr & M68K_TCR_ENABLED) {
            cpu_abort(cs, "Quetz cached RAM does not support address translation");
        }
        if (!(env->sr & SR_S)) {
            cpu_abort(cs, "Quetz cached RAM requires supervisor firmware");
        }
        const bool fetch = qemu_access_type == MMU_INST_FETCH;
        const hwaddr backing = (hwaddr)(address & TARGET_PAGE_MASK) +
            (fetch ? 0 : (UINT64_C(1) << 32));
        tlb_set_page(cs, address & TARGET_PAGE_MASK, backing,
                     fetch ? PAGE_EXEC : PAGE_READ | PAGE_WRITE,
                     mmu_idx, TARGET_PAGE_SIZE);
        return true;
    }

"""
replace(helper, "Quetz native RAM data route:",
        "    target_ulong page_size;\n\n"
        "    if ((env->mmu.tcr & M68K_TCR_ENABLED) == 0) {\n",
        "    target_ulong page_size;\n\n" + routing +
        "    if ((env->mmu.tcr & M68K_TCR_ENABLED) == 0) {\n",
        after="bool m68k_cpu_tlb_fill(")
print("ColdFire registered native RAM overlay applied")
