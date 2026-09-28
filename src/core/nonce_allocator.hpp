#pragma once

#include <atomic>
#include <cstdint>

namespace dcr {

class NonceAllocator {
private:
    std::atomic<uint64_t> current_nonce_{0};

public:
    struct Range { 
        uint32_t start; 
        uint32_t count; 
    };

    NonceAllocator() = default;

    Range allocate(uint32_t batch_size) {
        uint64_t start = current_nonce_.fetch_add(batch_size, std::memory_order_relaxed);
        return Range{ static_cast<uint32_t>(start & 0xFFFFFFFF), batch_size };
    }

    void reset() {
        current_nonce_.store(0, std::memory_order_relaxed);
    }

    uint64_t total_allocated() const {
        return current_nonce_.load(std::memory_order_relaxed);
    }
};

} // namespace dcr
