#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>
#include <memory>
#include <sys/mman.h>
#include <unistd.h>

#include "../../src/quetz_ipc_types.h"

extern "C" size_t quetz_c_layout(unsigned field);
extern "C" uint8_t *quetz_c_native_ram(QuetzSharedData *, unsigned);

using SST::Quetz::QuetzCommand;
using SST::Quetz::QUETZ_CMD_DATA_BYTES;
using SST::Quetz::QuetzInsnClass;
using SST::Quetz::QuetzSharedData;
using SST::Quetz::QuetzShmemCmd;

TEST_CASE("QuetzCommand layout") {
    CHECK(sizeof(QuetzCommand) >= 48);
    CHECK(alignof(QuetzCommand) >= 4);
    CHECK(offsetof(QuetzCommand, cmd) == 0);
    CHECK(offsetof(QuetzCommand, size) == 4);
    CHECK(offsetof(QuetzCommand, pc) == 8);
    CHECK(offsetof(QuetzCommand, addr) == 16);
    CHECK(offsetof(QuetzCommand, insn_class) == 24);
    CHECK(offsetof(QuetzCommand, data) == 32);
    CHECK(sizeof(QuetzCommand::data) == QUETZ_CMD_DATA_BYTES);
}

TEST_CASE("IPC enums") {
    CHECK(QuetzShmemCmd::QUETZ_CMD_NOP == 0);
    CHECK(QuetzShmemCmd::QUETZ_CMD_READ == 1);
    CHECK(QuetzShmemCmd::QUETZ_CMD_WRITE == 2);
    CHECK(QuetzShmemCmd::QUETZ_CMD_EXIT == 3);
    CHECK(QuetzInsnClass::QUETZ_INSN_CLASS_COUNT == 8);
}

TEST_CASE("QuetzSharedData layout") {
    CHECK(sizeof(QuetzSharedData) >= 24);
    CHECK(offsetof(QuetzSharedData, numCores) == 0);
    CHECK(offsetof(QuetzSharedData, mmio_slot) == 32);
    CHECK(QuetzShmemCmd::QUETZ_CMD_MMIO_READ_REQ == 4);
    CHECK(QuetzShmemCmd::QUETZ_CMD_MMIO_WRITE_REQ == 5);
}

TEST_CASE("QuetzIrqSlot layout") {
    using SST::Quetz::QuetzIrqSlot;
    using SST::Quetz::QUETZ_MAX_IRQ_LINES;
    CHECK(sizeof(QuetzIrqSlot) == 8);
    CHECK(offsetof(QuetzIrqSlot, seq) == 0);
    CHECK(offsetof(QuetzIrqSlot, level) == 4);
    CHECK(QUETZ_MAX_IRQ_LINES == 64);
    CHECK(offsetof(QuetzSharedData, irq_slot) ==
          offsetof(QuetzSharedData, mmio_req) +
          sizeof(QuetzSharedData::mmio_req));
}

TEST_CASE("canonical IPC header has identical C and C++ layout") {
    const size_t cpp[] = {sizeof(QuetzCommand), sizeof(QuetzSharedData),
        sizeof(QuetzShmemCmd), sizeof(QuetzInsnClass),
        offsetof(QuetzSharedData, cpu_reset_epoch),
        offsetof(QuetzSharedData, native_region_count),
        offsetof(QuetzSharedData, native_regions),
        offsetof(QuetzSharedData, native_ram_storage),
        offsetof(QuetzSharedData, magic), QUETZ_SHM_MAGIC};
    for (unsigned i = 0; i < sizeof(cpp) / sizeof(cpp[0]); ++i) CHECK(cpp[i] == quetz_c_layout(i));
    CHECK(sizeof(QuetzShmemCmd) == 4);
    CHECK(sizeof(QuetzInsnClass) == 4);
}

TEST_CASE("native RAM keeps one offset across different mmap alignment residues") {
    const size_t page = size_t(sysconf(_SC_PAGESIZE));
    if (page >= 65536) return;
    const size_t shared_offset = 128;
    const size_t length = (shared_offset + sizeof(QuetzSharedData) + page - 1) & ~(page - 1);
    const size_t gap = (length + 65535) & ~size_t(65535);
    const size_t reserved_size = 2 * gap + 3 * 65536;
    struct Resources {
        FILE* file = nullptr;
        void* mapping = MAP_FAILED;
        size_t size = 0;
        ~Resources() {
            if (mapping != MAP_FAILED) munmap(mapping, size);
            if (file) fclose(file);
        }
    } resources;
    resources.file = tmpfile();
    REQUIRE(resources.file != nullptr);
    REQUIRE(ftruncate(fileno(resources.file), length) == 0);
    resources.size = reserved_size;
    resources.mapping = mmap(nullptr, reserved_size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    REQUIRE(resources.mapping != MAP_FAILED);
    const uintptr_t first = (uintptr_t(resources.mapping) + 65535) & ~uintptr_t(65535);
    const uintptr_t second = first + gap + page;
    REQUIRE(mmap(reinterpret_cast<void*>(first), length, PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_FIXED, fileno(resources.file), 0) != MAP_FAILED);
    REQUIRE(mmap(reinterpret_cast<void*>(second), length, PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_FIXED, fileno(resources.file), 0) != MAP_FAILED);
    auto* sst = new(reinterpret_cast<void*>(first + shared_offset)) QuetzSharedData{};
    auto* qemu = new(reinterpret_cast<void*>(second + shared_offset)) QuetzSharedData;
    const QuetzNativeRamRegion regions[] = {{0x12000000,65536,0,0}, {0x23000000,65536,0,0}};
    REQUIRE(quetz_configure_native_regions(sst, regions, 2));
    CHECK((uintptr_t(sst) & 65535) != (uintptr_t(qemu) & 65535));
    for (unsigned bank = 0; bank < 2; ++bank) {
        auto* a = quetz_native_ram(sst, bank);
        auto* b = quetz_c_native_ram(qemu, bank);
        REQUIRE(a != nullptr); REQUIRE(b != nullptr);
        CHECK(a - reinterpret_cast<uint8_t*>(sst) == b - reinterpret_cast<uint8_t*>(qemu));
        CHECK((uintptr_t(a) & 65535) == 0);
        CHECK((uintptr_t(b) & (page - 1)) == 0);
        a[0] = uint8_t(0x21 + bank);
        b[65535] = uint8_t(0x81 + bank);
        CHECK(b[0] == 0x21 + bank);
        CHECK(a[65535] == 0x81 + bank);
    }
    CHECK(quetz_native_ram(sst, 2) == nullptr);
    CHECK(quetz_c_native_ram(qemu, 2) == nullptr);
    sst->native_regions[0].offset = 0;
    CHECK(quetz_native_ram(sst, 0) == nullptr);
    CHECK(quetz_c_native_ram(qemu, 0) == nullptr);
    sst->native_regions[0].offset = UINT32_MAX;
    CHECK(quetz_native_ram(sst, 0) == nullptr);
    CHECK(quetz_c_native_ram(qemu, 0) == nullptr);
}

TEST_CASE("native descriptors reject malformed capacity overlap and bounds") {
    auto shared = std::make_unique<QuetzSharedData>();
    QuetzNativeRamRegion regions[] = {{0x12000000,65536,0,0}, {0x23000000,4096,0,0}};
    REQUIRE(quetz_configure_native_regions(shared.get(), regions, 2));
    CHECK(quetz_native_regions_valid(shared.get()));
    auto good = shared->native_regions[1];
    shared->native_regions[1].base = regions[0].base;
    CHECK_FALSE(quetz_native_regions_valid(shared.get()));
    shared->native_regions[1] = good;
    shared->native_regions[1].offset = shared->native_regions[0].offset;
    CHECK_FALSE(quetz_native_regions_valid(shared.get()));
    shared->native_regions[1] = good;
    shared->native_regions[1].size = 65537;
    CHECK_FALSE(quetz_native_regions_valid(shared.get()));
    shared->native_regions[1] = good;
    shared->native_regions[1].base++;
    CHECK_FALSE(quetz_native_regions_valid(shared.get()));
    shared->native_regions[1] = good;
    shared->native_regions[1].reserved = 1;
    CHECK_FALSE(quetz_native_regions_valid(shared.get()));
    CHECK_FALSE(quetz_configure_native_regions(shared.get(), regions, 3));
    CHECK(quetz_configure_native_regions(shared.get(), nullptr, 0));
    CHECK(quetz_native_ram(shared.get(), 0) == nullptr);
}
