#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <sst/core/interprocess/tunneldef.h>
#include <sst/core/interprocess/shmparent.h>
#include <quetz/quetz_ipc_types.h>
#include <quetz/quetz_ipc_client.h>
#include <cstring>

class IpcTestTunnel : public SST::Core::Interprocess::TunnelDef<QuetzSharedData, QuetzCommand> {
    using Base = SST::Core::Interprocess::TunnelDef<QuetzSharedData, QuetzCommand>;
public:
    IpcTestTunnel(size_t cores, size_t size, uint32_t children) : Base(cores, size, children) {}
    uint32_t initialize(void* mapping) {
        auto result = Base::initialize(mapping);
        std::memset(sharedData, 0, sizeof(*sharedData));
        sharedData->numCores = getNumBuffers();
        sharedData->magic = QUETZ_SHM_MAGIC;
        return result;
    }
    QuetzSharedData* shared() { return sharedData; }
};

TEST_CASE("public QEMU client attaches to the installed SST tunnel layout") {
    SST::Core::Interprocess::SHMParent<IpcTestTunnel> parent(1, 2, 8, 2);
    struct Cleanup {
        std::string name;
        QuetzIpcClient* client = nullptr;
        ~Cleanup() { quetz_ipc_detach(client); shm_unlink(name.c_str()); }
    } cleanup{parent.getRegionName()};
    auto* shared = parent.getTunnel()->shared();
    const QuetzNativeRamRegion regions[] = {{0x12000000, 65536, 0, 0}, {0x23000000, 4096, 0, 0}};
    REQUIRE(quetz_configure_native_regions(shared, regions, 2));
    quetz_ipc_cpu_reset(1);
    cleanup.client = quetz_ipc_attach(cleanup.name.c_str());
    REQUIRE(cleanup.client != nullptr);
    CHECK(quetz_ipc_vcpu_count(cleanup.client) == 2);
    CHECK(quetz_ipc_native_region_count(cleanup.client) == 2);
    CHECK(shared->cpu_reset_epoch[1] == 1);
    quetz_ipc_cpu_reset(1);
    CHECK(shared->cpu_reset_epoch[1] == 2);
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t base = 0, size = 0;
        REQUIRE(quetz_ipc_native_region(cleanup.client, i, &base, &size));
        CHECK(base == regions[i].base); CHECK(size == regions[i].size);
        auto* a = quetz_native_ram(shared, i);
        auto* b = quetz_ipc_native_ram(cleanup.client, base, size);
        REQUIRE(a != nullptr); REQUIRE(b != nullptr);
        a[0] = uint8_t(0x31 + i); b[size - 1] = uint8_t(0x71 + i);
        CHECK(b[0] == 0x31 + i); CHECK(a[size - 1] == 0x71 + i);
        CHECK(quetz_ipc_native_ram(cleanup.client, base + 4096, size) == nullptr);
        CHECK(quetz_ipc_native_ram(cleanup.client, base, size / 2) == nullptr);
    }
    quetz_ipc_detach(cleanup.client); cleanup.client = nullptr;
    shared->magic = 0x515A4D04;
    CHECK(quetz_ipc_attach(cleanup.name.c_str()) == nullptr);
    shared->magic = QUETZ_SHM_MAGIC;
    shared->native_regions[0].offset = 0;
    CHECK(quetz_ipc_attach(cleanup.name.c_str()) == nullptr);
    REQUIRE(quetz_configure_native_regions(shared, nullptr, 0));
    cleanup.client = quetz_ipc_attach(cleanup.name.c_str());
    REQUIRE(cleanup.client != nullptr);
    CHECK(quetz_ipc_native_region_count(cleanup.client) == 0);
}
