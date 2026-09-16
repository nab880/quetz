#ifndef SST_QUETZ_CACHE_LAUNCH_H
#define SST_QUETZ_CACHE_LAUNCH_H
#include "quetz_platform_contract.h"
namespace SST { namespace Quetz {
inline const char* windowCacheLaunchError(const std::vector<std::string>& args,
                                          uint32_t vcpus, const std::string& machine) {
    return coldFireLaunchError(args, vcpus, machine);
}
} }
#endif
