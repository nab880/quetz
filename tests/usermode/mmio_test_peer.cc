// Exercise the real QEMU signal hook against an actual SST shared-memory tunnel.
#include <cstdlib>
#include <sst/core/interprocess/tunneldef.h>
#include <sst/core/interprocess/shmparent.h>
#include <quetz/quetz_ipc_types.h>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

class TestTunnel : public SST::Core::Interprocess::TunnelDef<QuetzSharedData, QuetzCommand> {
    using Base = SST::Core::Interprocess::TunnelDef<QuetzSharedData, QuetzCommand>;
public:
    TestTunnel(size_t cores, size_t size, uint32_t children) : Base(cores, size, children) {}
    uint32_t initialize(void* mapping) {
        auto remaining = Base::initialize(mapping);
        std::memset(sharedData, 0, sizeof(*sharedData));
        sharedData->numCores = getNumBuffers();
        sharedData->magic = QUETZ_SHM_MAGIC;
        return remaining;
    }
    QuetzSharedData* shared() { return sharedData; }
};

int main(int argc, char** argv) {
    if (argc != 4 || (std::strcmp(argv[1], "m68k") && std::strcmp(argv[1], "riscv64"))) {
        std::fprintf(stderr, "usage: mmio_test_peer m68k|riscv64 QEMU GUEST\n");
        return 2;
    }
    const bool m68k = !std::strcmp(argv[1], "m68k");
    SST::Core::Interprocess::SHMParent<TestTunnel> parent(1, 4, 8, 1);
    struct Unlink { std::string name; ~Unlink() { shm_unlink(name.c_str()); } } cleanup{parent.getRegionName()};
    auto* shared = parent.getTunnel()->shared();
    const auto range = "shmname=" + cleanup.name + ",base=0x70000000,size=0x1000";
    pid_t child = fork();
    if (child < 0) return 2;
    if (!child) {
        execl(argv[2], argv[2], "-sst-mmio-range", range.c_str(), argv[3], nullptr);
        _exit(127);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    unsigned requests[4]{}, reads = 0, writes = 0;
    bool valid = true;
    int status = 0;
    while (waitpid(child, &status, WNOHANG) == 0) {
        for (unsigned v = 0; v < 4; ++v) {
            auto& req = shared->mmio_req[v];
            if (!__atomic_load_n(&req.pending, __ATOMIC_ACQUIRE)) continue;
            const auto addr = req.addr;
            const auto size = req.size;
            const auto cmd = req.cmd;
            const auto value = req.write_val;
            __atomic_store_n(&req.pending, 0u, __ATOMIC_RELEASE);
            uint64_t answer = 0;
            if (cmd == QUETZ_CMD_MMIO_READ_REQ) {
                ++reads;
                if (m68k) {
                    valid &= v == 0 && (size == 1 || size == 2 || size == 4);
                    if (addr == 0x70000004) answer = 1;
                    else if (addr == 0x70000008 && size && size <= 4)
                        answer = uint64_t(1) << (size * 8 - 1);
                    else valid &= addr == 0x70000000;
                } else {
                    valid &= addr == 0x70000000 && size == 8;
                    answer = 0x100 + v;
                }
            } else if (cmd == QUETZ_CMD_MMIO_WRITE_REQ) {
                ++writes;
                valid &= addr == 0x70000010;
                if (m68k) {
                    valid &= v == 0 && (size == 1 || size == 2 || size == 4);
                    valid &= value == 0 || value == 1 || (size && size <= 4 &&
                             value == (uint64_t(1) << (size * 8 - 1)));
                } else valid &= size == 8 && value == v;
            } else valid = false;
            shared->mmio_slot[v].value = answer;
            __atomic_store_n(&shared->mmio_slot[v].ready, 1u, __ATOMIC_RELEASE);
            ++requests[v];
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(child, SIGKILL);
            waitpid(child, &status, 0);
            std::fprintf(stderr, "QEMU MMIO regression timed out\n");
            valid = false;
            break;
        }
        usleep(50);
    }
    valid &= WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (m68k) valid &= reads == 82 && writes == 18 && requests[0] == 100;
    else valid &= requests[0] == 512 && requests[1] == 256 &&
                  requests[2] == 256 && requests[3] == 256;
    std::printf("%s: %s, reads=%u writes=%u per-vcpu=[%u,%u,%u,%u]\n",
                argv[1], valid ? "PASS" : "FAIL", reads, writes,
                requests[0], requests[1], requests[2], requests[3]);
    return valid ? 0 : 1;
}
