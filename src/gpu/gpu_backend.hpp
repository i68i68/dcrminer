#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../common/types.hpp"

namespace dcr {

class GpuBackend {
public:
    virtual ~GpuBackend() = default;
    virtual std::string name() const = 0;
    virtual BackendType type() const = 0;
    virtual bool init(int device_id) = 0;
    virtual void set_job(const ChainValue& midstate, const MsgBlock& block2, const Target256& target) = 0;
    virtual uint64_t mine(uint32_t start_nonce, uint32_t count) = 0; // returns hashes done
    virtual std::vector<Solution> get_solutions() = 0;
    virtual void shutdown() = 0;
    virtual std::string device_name() const = 0;
};

} // namespace dcr
