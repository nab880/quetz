#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "quetz_platform_contract.h"
using SST::Quetz::parseNativeRegions;
TEST_CASE("native region configuration is explicit and bounded") {
    CHECK(parseNativeRegions("").empty());
    const auto r = parseNativeRegions("0x12000000:0x10000;0x23000000:0x1000");
    REQUIRE(r.size() == 2);
    CHECK(r[0].base == 0x12000000); CHECK(r[1].size == 4096);
    CHECK(parseNativeRegions("0xfffff000:0x1000").size() == 1);
    for (const char* invalid : {";", "0:4096;", "1:4096", "0:1", "0:0", "0:65537",
        "0:4096;0:4096", "0:4096;4096:4096;8192:4096", "0xfffff000:8192",
        "0x100000000:4096", "0:-1", "-4096:4096", "0:4096:1", "x:4096"})
        CHECK_THROWS(parseNativeRegions(invalid));
}
