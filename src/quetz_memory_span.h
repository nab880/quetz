#ifndef QUETZ_MEMORY_SPAN_H
#define QUETZ_MEMORY_SPAN_H

#include <algorithm>
#include <cstdint>
#include <limits>

namespace SST { namespace Quetz {

inline bool memorySpanValid(uint64_t address, uint64_t bytes) {
    return bytes == 0 || bytes - 1 <= std::numeric_limits<uint64_t>::max() - address;
}

// Subtract the offset instead of rounding the address upward: the last line
// in the address space has no representable exclusive end. A zero line size
// denotes a memory interface without a cache-line restriction.
inline uint64_t memoryChunkSize(uint64_t address, uint64_t remaining,
                                uint64_t line_size, uint64_t request_limit) {
    uint64_t bytes = std::min(remaining, request_limit);
    return line_size ? std::min(bytes, line_size - address % line_size) : bytes;
}

inline uint32_t memorySlots(uint64_t address, uint32_t bytes, uint64_t line_size) {
    if (bytes == 0) return 0;
    return static_cast<uint32_t>(1 + (address % line_size + uint64_t(bytes) - 1) / line_size);
}

} }
#endif
