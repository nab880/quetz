#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../../qemu-overlay/linux-user/sst_mmio_m68k.h"

static uint16_t moveOp(unsigned size, unsigned srcMode, unsigned srcReg,
                       unsigned dstMode, unsigned dstReg) {
    const unsigned field = size == 1 ? 1 : size == 2 ? 3 : 2;
    return uint16_t((field << 12) | (dstReg << 9) | (dstMode << 6) |
                    (srcMode << 3) | srcReg);
}

TEST_CASE("MOVE condition codes use operand width and preserve only X") {
    for (unsigned size : {1, 2, 4}) {
        const uint32_t sign = 1u << (size * 8 - 1);
        for (uint32_t previous = 0; previous < 32; ++previous) {
            CHECK(m68k_mmio_move_ccr(0, size, previous) == ((previous & 16) | 4));
            CHECK(m68k_mmio_move_ccr(1, size, previous) == (previous & 16));
            CHECK(m68k_mmio_move_ccr(sign, size, previous) == ((previous & 16) | 8));
            CHECK(m68k_mmio_move_ccr(UINT32_MAX, size, previous) == ((previous & 16) | 8));
            if (size < 4)
                CHECK(m68k_mmio_move_ccr(1u << (size * 8), size, previous) ==
                      ((previous & 16) | 4));
        }
    }
}

TEST_CASE("MOVE and MOVEA decode separately at every operand width") {
    for (unsigned size : {1, 2, 4}) {
        for (unsigned reg = 0; reg < 8; ++reg) {
            int store, decodedReg;
            unsigned width, index;
            REQUIRE(m68k_mmio_decode(moveOp(size, 2, 0, 0, reg),
                                    &store, &width, &decodedReg, &index) == 2);
            CHECK(store == 0); CHECK(width == size); CHECK(decodedReg == int(reg));
            CHECK(index == 0);
            REQUIRE(m68k_mmio_decode(moveOp(size, 0, reg, 2, 0),
                                    &store, &width, &decodedReg, &index) == 2);
            CHECK(store == 1); CHECK(decodedReg == int(reg));
            const int len = m68k_mmio_decode(moveOp(size, 2, 0, 1, reg),
                                             &store, &width, &decodedReg, &index);
            if (size == 1) CHECK(len == 0);
            else {
                CHECK(len == 2); CHECK(store == 0); CHECK(decodedReg == int(reg + 8));
            }
        }
    }
}

TEST_CASE("unsupported MOVE forms cannot silently corrupt address registers or PC") {
    int store, reg;
    unsigned size, index;
    CHECK(m68k_mmio_decode(moveOp(1, 1, 0, 2, 0), &store, &size, &reg, &index) == 0);
    CHECK(m68k_mmio_decode(moveOp(4, 0, 0, 7, 2), &store, &size, &reg, &index) == 0);
    for (unsigned mode : {3, 4}) {
        CHECK(m68k_mmio_decode(moveOp(4, mode, 0, 0, 0), &store, &size, &reg, &index) == 0);
        CHECK(m68k_mmio_decode(moveOp(4, 0, 0, mode, 0), &store, &size, &reg, &index) == 0);
    }
    CHECK(m68k_mmio_decode(0x4e71, &store, &size, &reg, &index) == 0);
    CHECK(m68k_mmio_decode(moveOp(4, 7, 4, 6, 0), &store, &size, &reg, &index) == 8);
    CHECK(store == 1); CHECK(reg == -1); CHECK(index == 6);
    CHECK(m68k_mmio_decode(moveOp(2, 7, 3, 0, 0), &store, &size, &reg, &index) == 4);
    CHECK(index == 2);
}
