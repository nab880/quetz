#include <quetz/quetz_ipc_types.h>
size_t quetz_c_layout(unsigned field) {
    const size_t values[] = {sizeof(QuetzCommand), sizeof(QuetzSharedData),
        sizeof(QuetzShmemCmd), sizeof(QuetzInsnClass),
        offsetof(QuetzSharedData, cpu_reset_epoch),
        offsetof(QuetzSharedData, native_region_count),
        offsetof(QuetzSharedData, native_regions),
        offsetof(QuetzSharedData, native_ram_storage),
        offsetof(QuetzSharedData, magic), QUETZ_SHM_MAGIC};
    return values[field];
}
uint8_t *quetz_c_native_ram(QuetzSharedData *shared, unsigned region) {
    return quetz_native_ram(shared, region);
}
