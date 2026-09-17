#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "quetz_memory_span.h"
using namespace SST::Quetz;

// Production arithmetic shared by the emitter and GPU DMA.

TEST_CASE("cache line slots") {
    const uint64_t line = 64;
    CHECK(memorySlots(0, 1, line) == 1);
    CHECK(memorySlots(0, 64, line) == 1);
    CHECK(memorySlots(60, 8, line) == 2);
    CHECK(memorySlots(0, 128, line) == 2);
    CHECK(memorySlots(0, 0, line) == 0);
}

TEST_CASE("split extra requests") {
    const uint32_t parts = memorySlots(60, 16, 64);
    CHECK(parts == 2);
    CHECK(parts - 1 == 1);
}

TEST_CASE("last cache line does not overflow while choosing a chunk") {
    CHECK(memoryChunkSize(UINT64_MAX - 31, 32, 64, 64) == 32);
    CHECK(memorySpanValid(UINT64_MAX - 31, 32));
    CHECK_FALSE(memorySpanValid(UINT64_MAX - 31, 33));
    CHECK(memoryChunkSize(4, 128, 16, 64) == 12);
    CHECK(memoryChunkSize(4, 128, 0, 64) == 64);
}
