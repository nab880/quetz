#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../../src/quetz_cache_launch.h"
static const char* windowCacheLaunchError(const std::vector<std::string>& args, uint32_t cpus = 1) {
    return SST::Quetz::windowCacheLaunchError(args, cpus, "example-v4e");
}

TEST_CASE("cache window accepts only the explicit V4e platform descriptor") {
    CHECK(windowCacheLaunchError({"-M", "example-v4e"}) == nullptr);
    CHECK(windowCacheLaunchError({"-machine", "example-v4e,board-option=on",
                                 "-display", "none", "-monitor", "none"}) == nullptr);
    CHECK(windowCacheLaunchError({"-machine=example-v4e", "-cpu", "cfv4e"}) == nullptr);
    CHECK(windowCacheLaunchError({"--machine", "example-v4e", "--cpu=cfv4e"}) == nullptr);
    CHECK(windowCacheLaunchError({"-serial", "stdio", "-machine", "example-v4e"}) == nullptr);
}

TEST_CASE("cache window rejects absent or conflicting CPU and machine arguments") {
    CHECK(windowCacheLaunchError({}) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "mcf5208evb", "-cpu", "cfv4e"}) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "example-v4e", "-cpu", "m5208"}) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "example-v4e", "-cpu"}) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "example-v4e", "-machine=none"}) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "none", "-machine=example-v4e"}) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "example-v4e", "--cpu=m68040"}) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "example-v4e,type=mcf5208evb"}) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "example-v4e,board-option=on,type=none"}) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "example-v4e", "-readconfig", "board.cfg"}) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "example-v4e", "--readconfig=board.cfg"}) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "example-v4e", "-M", "example-v4e"}) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "example-v4e", "-cpu", "cfv4e", "-cpu", "cfv4e"}) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "example-v4e", "-global", "x=y"}) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "example-v4e", "-smp", "2"}) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "example-v4e", "-accel", "tcg,thread=multi"}) != nullptr);
}

TEST_CASE("two private caches require the complete two-CPU launch contract") {
    const std::vector<std::string> valid = {"-M", "example-v4e,secondary-kernel=second.elf",
                                           "-smp", "2", "-accel", "tcg,thread=single"};
    CHECK(windowCacheLaunchError(valid, 2) == nullptr);
    CHECK(windowCacheLaunchError(valid, 1) != nullptr);
    CHECK(windowCacheLaunchError(valid, 0) != nullptr);
    CHECK(windowCacheLaunchError(valid, 3) != nullptr);
    CHECK(windowCacheLaunchError({"-M", "example-v4e"}, 2) != nullptr);
    for (const std::vector<std::string>& override : {
             std::vector<std::string>{"-accel", "tcg,thread=multi"},
             {"-cpu", "m5208"}, {"-readconfig", "a.cfg"}, {"-smp", "1"}}) {
        auto args = valid;
        args.insert(args.end(), override.begin(), override.end());
        CHECK(windowCacheLaunchError(args, 2) != nullptr);
    }
    auto routes = valid;
    routes[1] += ",board-option=40";
    CHECK(windowCacheLaunchError(routes, 2) == nullptr);
}
