#ifndef SST_QUETZ_PLATFORM_CONTRACT_H
#define SST_QUETZ_PLATFORM_CONTRACT_H
#include <quetz/quetz_ipc_types.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace SST { namespace Quetz {
inline std::vector<QuetzNativeRamRegion> parseNativeRegions(const std::string& value) {
    std::vector<QuetzNativeRamRegion> regions;
    size_t begin = 0;
    while (begin < value.size()) {
        const auto end = value.find(';', begin);
        const auto item = value.substr(begin, end == std::string::npos ? end : end - begin);
        const auto colon = item.find(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 == item.size() ||
            item.find(':', colon + 1) != std::string::npos)
            throw std::invalid_argument("cache_native_regions requires base:size pairs separated by semicolons");
        auto number = [](const std::string& text) {
            if (text.empty() || text.find_first_not_of("0123456789abcdefABCDEFxX") != std::string::npos)
                throw std::invalid_argument("invalid native region number");
            size_t used = 0;
            const uint64_t result = std::stoull(text, &used, 0);
            if (used != text.size()) throw std::invalid_argument("invalid native region number");
            return result;
        };
        const uint64_t base = number(item.substr(0, colon));
        const uint64_t size = number(item.substr(colon + 1));
        if (!size || size > QUETZ_NATIVE_REGION_BYTES || (size & 4095) || (base & 4095) ||
            base >= (UINT64_C(1) << 32) || size > (UINT64_C(1) << 32) - base)
            throw std::invalid_argument("native regions require 4-KiB alignment, at most 64 KiB each and 32-bit guest addresses");
        for (const auto& region : regions)
            if (base < uint64_t(region.base) + region.size && region.base < base + size)
                throw std::invalid_argument("native regions overlap");
        if (regions.size() == QUETZ_MAX_NATIVE_REGIONS)
            throw std::invalid_argument("at most two native regions are supported");
        regions.push_back({uint32_t(base), uint32_t(size), 0, 0});
        if (end == std::string::npos) break;
        begin = end + 1;
        if (begin == value.size()) throw std::invalid_argument("empty native region");
    }
    return regions;
}

inline const char* coldFireLaunchError(const std::vector<std::string>& args,
                                       uint32_t vcpus, const std::string& machine_name) {
    if (vcpus < 1 || vcpus > 2) return "ColdFire adapter requires one or two CPUs";
    if (machine_name.empty() || machine_name.find_first_of(", \t\n=") != std::string::npos)
        return "ColdFire adapter requires an explicit platform_machine name";
    bool machine_seen = false, cpu_seen = false, smp_seen = false, accel_seen = false;
    for (size_t i = 0; i < args.size(); ++i) {
        std::string option = args[i];
        if (option.rfind("--", 0) == 0) option.erase(0, 1);
        const auto equal = option.find('=');
        const auto key = option.substr(0, equal);
        if (key == "-readconfig" || key == "-global" || key == "-enable-kvm")
            return "ColdFire adapter forbids external model overrides";
        const bool machine = key == "-machine" || key == "-M";
        if (!machine && key != "-cpu" && key != "-smp" && key != "-accel") continue;
        std::string value;
        if (equal != std::string::npos) value = option.substr(equal + 1);
        else if (++i < args.size()) value = args[i];
        else return "missing QEMU model option value";
        if (machine) {
            if (machine_seen || value.substr(0, value.find(',')) != machine_name ||
                value.find(",type=") != std::string::npos ||
                value.find(",accel=") != std::string::npos)
                return "QEMU machine does not match the platform descriptor";
            machine_seen = true;
        } else if (key == "-cpu") {
            if (cpu_seen || value != "cfv4e") return "ColdFire adapter supports only one cfv4e selection";
            cpu_seen = true;
        } else if (key == "-smp") {
            if (smp_seen || value != std::to_string(vcpus)) return "QEMU topology does not match configured CPU count";
            smp_seen = true;
        } else {
            if (accel_seen || (value != "tcg,thread=single" && !(vcpus == 1 && value == "tcg")))
                return "ColdFire adapter requires single-thread TCG";
            accel_seen = true;
        }
    }
    if (!machine_seen) return "ColdFire adapter requires an explicit QEMU machine";
    if (vcpus == 2 && (!smp_seen || !accel_seen)) return "two CPUs require explicit -smp 2 and single-thread TCG";
    return nullptr;
}
} }
#endif
