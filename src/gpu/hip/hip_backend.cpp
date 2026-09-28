#include "hip_backend.hpp"

#if DCR_ENABLE_HIP
#include <iostream>

extern "C" void launch_hip_kernel(const uint32_t* midstate, const uint32_t* block2, const uint32_t* target, uint32_t start_nonce, uint32_t count, void* d_results, uint32_t* d_result_count, hipStream_t stream);

namespace dcr {

HipBackend::HipBackend() = default;

HipBackend::~HipBackend() {
    shutdown();
}

bool HipBackend::init(int device_id) {
    device_id_ = device_id;
    if (hipSetDevice(device_id_) != hipSuccess) return false;
    
    hipDeviceProp_t prop;
    hipGetDeviceProperties(&prop, device_id_);
    dev_name_ = prop.name;

    hipStreamCreate(&stream_);
    
    hipMalloc(&d_results_, 16 * sizeof(SolutionKernel));
    hipMalloc(&d_result_count_, sizeof(uint32_t));
    
    hipHostMalloc(&h_results_, 16 * sizeof(SolutionKernel));
    hipHostMalloc(&h_result_count_, sizeof(uint32_t));
    
    return true;
}

void HipBackend::set_job(const ChainValue& midstate, const MsgBlock& block2, const Target256& target) {
    midstate_ = midstate;
    block2_ = block2;
    target_ = target;
}

uint64_t HipBackend::mine(uint32_t start_nonce, uint32_t count) {
    *h_result_count_ = 0;
    hipMemcpyAsync(d_result_count_, h_result_count_, sizeof(uint32_t), hipMemcpyHostToDevice, stream_);
    
    launch_hip_kernel(midstate_.data(), block2_.data(), target_.data(), start_nonce, count, d_results_, d_result_count_, stream_);
    
    hipMemcpyAsync(h_result_count_, d_result_count_, sizeof(uint32_t), hipMemcpyDeviceToHost, stream_);
    hipMemcpyAsync(h_results_, d_results_, 16 * sizeof(SolutionKernel), hipMemcpyDeviceToHost, stream_);
    hipStreamSynchronize(stream_);
    
    return count;
}

std::vector<Solution> HipBackend::get_solutions() {
    std::vector<Solution> res;
    uint32_t c = *h_result_count_;
    if (c > 16) c = 16;
    for (uint32_t i = 0; i < c; ++i) {
        Solution sol;
        sol.nonce = h_results_[i].nonce;
        sol.device_id = static_cast<uint32_t>(device_id_);
        for (int j = 0; j < 8; ++j) {
            sol.hash_words[j] = h_results_[i].hash[j];
            write_le32(sol.pow_hash.data() + j * 4, h_results_[i].hash[j]);
        }
        res.push_back(sol);
    }
    return res;
}

void HipBackend::shutdown() {
    if (stream_) {
        hipStreamDestroy(stream_);
        stream_ = nullptr;
    }
    if (d_results_) { hipFree(d_results_); d_results_ = nullptr; }
    if (d_result_count_) { hipFree(d_result_count_); d_result_count_ = nullptr; }
    if (h_results_) { hipHostFree(h_results_); h_results_ = nullptr; }
    if (h_result_count_) { hipHostFree(h_result_count_); h_result_count_ = nullptr; }
}

std::string HipBackend::device_name() const {
    return dev_name_;
}

} // namespace dcr

#endif
