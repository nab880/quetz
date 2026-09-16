#ifndef SST_QUETZ_MULTICORE_LAUNCH_H
#define SST_QUETZ_MULTICORE_LAUNCH_H
#include "quetz_platform_contract.h"
namespace SST { namespace Quetz {
inline const char* multicoreLaunchError(uint32_t vcpus, bool,
                                       const std::vector<std::string>& args,
                                       const std::string& machine) {
    if (vcpus != 2) return "system multicore requires exactly two CPUs";
    return coldFireLaunchError(args, vcpus, machine);
}
} }
#endif
