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

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

TEST_CASE("competing clients own a mailbox until their own response is consumed") {
    SST::Core::Interprocess::SHMParent<IpcTestTunnel> parent(1, 2, 8, 2);
    struct Cleanup {
        std::string name;
        QuetzIpcClient* a = nullptr;
        QuetzIpcClient* b = nullptr;
        ~Cleanup() { quetz_ipc_detach(a); quetz_ipc_detach(b); shm_unlink(name.c_str()); }
    } cleanup{parent.getRegionName()};
    auto* shared = parent.getTunnel()->shared();
    cleanup.a = quetz_ipc_attach(cleanup.name.c_str());
    cleanup.b = quetz_ipc_attach(cleanup.name.c_str());
    REQUIRE(cleanup.a != nullptr); REQUIRE(cleanup.b != nullptr);
    std::atomic<unsigned> finished{0};
    uint64_t values[2]{};
    auto read = [&](unsigned which) {
        values[which] = quetz_ipc_mmio_read(which ? cleanup.b : cleanup.a,
                                           0, which ? 0x2222 : 0x1111, 4);
        ++finished;
    };
    std::thread first(read, 0);
    while (!__atomic_load_n(&shared->mmio_req[0].pending, __ATOMIC_ACQUIRE))
        std::this_thread::yield();
    std::thread second(read, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    // The old code deterministically replaced the still-pending first request.
    CHECK(shared->mmio_req[0].addr == 0x1111);
    for (unsigned i = 0; i < 2; ++i) {
        auto& req = shared->mmio_req[0];
        while (!__atomic_load_n(&req.pending, __ATOMIC_ACQUIRE)) std::this_thread::yield();
        const auto addr = req.addr;
        __atomic_store_n(&req.pending, 0u, __ATOMIC_RELEASE);
        shared->mmio_slot[0].value = addr ^ 0xA5A5;
        __atomic_store_n(&shared->mmio_slot[0].ready, 1u, __ATOMIC_RELEASE);
    }
    first.join(); second.join();
    CHECK(finished == 2);
    CHECK(values[0] == (0x1111 ^ 0xA5A5));
    CHECK(values[1] == (0x2222 ^ 0xA5A5));
    CHECK(shared->mmio_req[0].busy == 0);
}

TEST_CASE("concurrent reads and writes preserve values and per-vCPU mailbox identity") {
    SST::Core::Interprocess::SHMParent<IpcTestTunnel> parent(1, 2, 8, 2);
    struct Cleanup {
        std::string name;
        QuetzIpcClient* client = nullptr;
        ~Cleanup() { quetz_ipc_detach(client); shm_unlink(name.c_str()); }
    } cleanup{parent.getRegionName()};
    auto* shared = parent.getTunnel()->shared();
    cleanup.client = quetz_ipc_attach(cleanup.name.c_str());
    REQUIRE(cleanup.client != nullptr);
    constexpr unsigned producers = 8, iterations = 128;
    std::atomic<unsigned> done{0}, badReads{0};
    unsigned requests[2]{}, badWrites = 0;
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < producers; ++t) threads.emplace_back([&, t] {
        for (unsigned i = 0; i < iterations; ++i) {
            const uint64_t addr = (uint64_t(t) << 32) | (i << 2);
            quetz_ipc_mmio_write(cleanup.client, t % 2, addr, 4, addr ^ 0xBEEF);
            if (quetz_ipc_mmio_read(cleanup.client, t % 2, addr, 4) != (addr ^ 0xCAFE))
                ++badReads;
        }
        ++done;
    });
    while (done != producers) {
        for (unsigned v = 0; v < 2; ++v) {
            auto& req = shared->mmio_req[v];
            if (!__atomic_load_n(&req.pending, __ATOMIC_ACQUIRE)) continue;
            const auto addr = req.addr;
            if ((addr >> 32) % 2 != v || req.size != 4) ++badWrites;
            if (req.cmd == QUETZ_CMD_MMIO_WRITE_REQ && req.write_val != (addr ^ 0xBEEF))
                ++badWrites;
            __atomic_store_n(&req.pending, 0u, __ATOMIC_RELEASE);
            shared->mmio_slot[v].value = addr ^ 0xCAFE;
            __atomic_store_n(&shared->mmio_slot[v].ready, 1u, __ATOMIC_RELEASE);
            ++requests[v];
        }
        std::this_thread::yield();
    }
    for (auto& thread : threads) thread.join();
    CHECK(badReads == 0); CHECK(badWrites == 0);
    CHECK(requests[0] == producers * iterations);
    CHECK(requests[1] == producers * iterations);
}
