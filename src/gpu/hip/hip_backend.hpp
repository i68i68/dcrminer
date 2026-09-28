#pragma once
#include "../gpu_backend.hpp"

#if DCR_ENABLE_HIP
#include <hip/hip_runtime.h>

namespace dcr {

class HipBackend : public GpuBackend {
public:
    HipBackend();
    ~HipBackend() override;

    std::string name() const override { return "HIP"; }
    BackendType type() const override { return BackendType::HIP; }
    bool init(int device_id) override;
    void set_job(const ChainValue& midstate, const MsgBlock& block2, const Target256& target) override;
    uint64_t mine(uint32_t start_nonce, uint32_t count) override;
    std::vector<Solution> get_solutions() override;
    void shutdown() override;
    std::string device_name() const override;

private:
    int device_id_ = -1;
    hipStream_t stream_ = nullptr;
    
    struct SolutionKernel {
        uint32_t nonce;
        uint32_t hash[8];
    };
    
    SolutionKernel* d_results_ = nullptr;
    uint32_t* d_result_count_ = nullptr;
    
    SolutionKernel* h_results_ = nullptr;
    uint32_t* h_result_count_ = nullptr;
    
    ChainValue midstate_;
    MsgBlock block2_;
    Target256 target_;
    
    std::string dev_name_;
};

} // namespace dcr

#endif
