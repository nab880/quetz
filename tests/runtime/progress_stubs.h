// Minimal SST transport plumbing for source-exact runtime regression tests.
// The production pipeline, output, emitter and async port are compiled intact.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <inttypes.h>
#include <limits>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <cerrno>
#define CALL_INFO 0
#define SST_ELI_REGISTER_SUBCOMPONENT(...)
#define SST_ELI_REGISTER_SUBCOMPONENT_API(...)
#define SST_ELI_DOCUMENT_PARAMS(...)
#include "quetz_ipc_types.h"
#include "quetz_memory_span.h"
namespace SST {
using ComponentId_t = uint64_t;
using TimeConverter = uint64_t;
using Cycle_t = uint64_t;
struct Params {
    std::unordered_map<std::string, std::string> values;
    template<class T> T find(const char* name, T fallback) const {
        auto it = values.find(name);
        if (it == values.end()) return fallback;
        if constexpr (std::is_same_v<T, std::string>) return it->second;
        else return static_cast<T>(std::stoull(it->second, nullptr, 0));
    }
};
struct Output {
    enum { STDOUT };
    template<class... A> void init(A...) {}
    template<class... A> void verbose(A...) {}
    template<class... A> [[noreturn]] void fatal(int, int, const char* fmt, A... args) {
        char message[1024]; std::snprintf(message, sizeof message, fmt, args...);
        throw std::runtime_error(message);
    }
};
struct SubComponent { explicit SubComponent(ComponentId_t) {} virtual ~SubComponent() = default; };
struct ComponentExtension { uint64_t getCurrentSimTime(TimeConverter) { return 0; } };
namespace Statistics { template<class T> struct Statistic { T value = 0; void addData(T n) { value += n; } }; }
namespace Interfaces {
struct StandardMem {
    struct Request {
        using id_t = uint64_t;
        inline static id_t next = 0;
        id_t id = ++next; uint64_t pAddr; size_t size;
        Request(uint64_t a, size_t n): pAddr(a), size(n) {}
        virtual ~Request() = default;
        id_t getID() const { return id; }
    };
    struct Read: Request { Read(uint64_t a, size_t n, int = 0, uint64_t = 0): Request(a,n) {} };
    struct Write: Request {
        std::vector<uint8_t> data;
        Write(uint64_t a,size_t n,const std::vector<uint8_t>& d,bool = false,int = 0,uint64_t = 0): Request(a,n),data(d) {}
    };
    struct ReadResp: Request { std::vector<uint8_t> data; explicit ReadResp(const Request& r): Request(r) {} };
    struct WriteResp: Request { explicit WriteResp(const Request& r): Request(r) {} };
    struct FlushAddr: Request { FlushAddr(uint64_t a, size_t n, bool, int): Request(a,n) {} };
    struct FlushResp: Request { explicit FlushResp(const Request& r): Request(r) {} };
    std::deque<Request*> sent;
    uint64_t line_size = 64;
    uint64_t getLineSize() const { return line_size; }
    void send(Request* r) { sent.push_back(r); }
    ~StandardMem() { for (auto* r: sent) delete r; }
};
}
namespace Quetz {
struct QuetzCoreBackend {
    std::queue<QuetzCommand> tunnel;
    unsigned reads = 0;
    bool readCommandNB(uint32_t, QuetzCommand* command) {
        ++reads;
        if (tunnel.empty()) return false;
        *command = tunnel.front();
        tunnel.pop();
        return true;
    }
    void updateSimTime(uint64_t) {}
    void incrementCycles() {}
};
struct MemRegionTable {};
struct MemRegionHandler { enum class Action { FORWARD, END_SIM, FORWARD_MMIO }; };
}
}
