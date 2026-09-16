/* Optional QEMU ColdFire adapter API; include after the target cpu.h. */
#ifndef QUETZ_COLDFIRE_CACHE_H
#define QUETZ_COLDFIRE_CACHE_H
#include <stdbool.h>
#include "quetz/quetz_ipc_client.h"
#include "quetz/quetz_ipc_types.h"
static inline bool quetz_coldfire_configure_cache(CPUM68KState *env, QuetzIpcClient *client)
{
    const unsigned count = quetz_ipc_native_region_count(client);
    if (count > QUETZ_MAX_NATIVE_REGIONS) return false;
    for (unsigned i = 0; i < count; ++i)
        if (!quetz_ipc_native_region(client, i, &env->quetz_cache_region_base[i],
                                    &env->quetz_cache_region_size[i])) return false;
    env->quetz_cache_region_count = count;
    env->quetz_cache_ram = count != 0;
    return true;
}
#endif
