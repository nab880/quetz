#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "../../src/quetz_multicore_launch.h"
static const char* multicoreLaunchError(uint32_t cpus, bool cached, const std::vector<std::string>& args) {
    return SST::Quetz::multicoreLaunchError(cpus, cached, args, "example-v4e");
}
using Args = std::vector<std::string>;

static Args valid() {
    return {"-M", "example-v4e,board-option=on",
            "-smp", "2", "-accel", "tcg,thread=single", "-nographic"};
}
TEST_CASE("two-core launch requires explicit topology and serialized TCG") {
    CHECK(multicoreLaunchError(2, false, valid()) == nullptr);
    CHECK(multicoreLaunchError(2, false, {"--machine=example-v4e",
          "-smp=2", "-accel=tcg,thread=single", "-cpu=cfv4e"}) == nullptr);
    CHECK(multicoreLaunchError(1, false, valid()) != nullptr);
    CHECK(multicoreLaunchError(3, false, valid()) != nullptr);
    CHECK(multicoreLaunchError(2, true, valid()) == nullptr);
    for (size_t index : {size_t(0), size_t(2), size_t(4)}) {
        auto args = valid(); args.erase(args.begin() + index, args.begin() + index + 2);
        CHECK(multicoreLaunchError(2, false, args) != nullptr);
    }
}
TEST_CASE("unsupported machine CPU accelerator and topology cannot override contract") {
    for (const std::string machine : {"other-board", "example-v4e,type=other-board", "example-v4e,accel=kvm"}) {
        auto args = valid(); args[1] = machine;
        CHECK(multicoreLaunchError(2, false, args) != nullptr);
    }
    for (const Args& override : {Args{"-M", "mcf5208evb"}, {"-machine", "example-v4e"},
         {"-cpu", "m5208"}, {"-cpu", "cfv4e,feature=on"}, {"-smp", "2"},
         {"-accel", "tcg,thread=multi"}, {"-readconfig", "x"}, {"-global", "x"}, {"-enable-kvm"}}) {
        auto args = valid(); args.insert(args.end(), override.begin(), override.end());
        CHECK(multicoreLaunchError(2, false, args) != nullptr);
    }
    for (const std::string count : {"1", "3", "2,sockets=2", "2,maxcpus=3", ""}) {
        auto args = valid(); args[3] = count;
        CHECK(multicoreLaunchError(2, false, args) != nullptr);
    }
    for (const std::string accel : {"tcg", "tcg,thread=multi", "kvm"}) {
        auto args = valid(); args[5] = accel;
        CHECK(multicoreLaunchError(2, false, args) != nullptr);
        CHECK(multicoreLaunchError(2, true, args) != nullptr);
    }
}
