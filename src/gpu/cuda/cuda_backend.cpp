#include "cuda_backend.hpp"

#if DCR_ENABLE_CUDA
#include <iostream>

extern "C" void launch_cuda_kernel(const uint32_t* midstate, const uint32_t* block2, const uint32_t* target, uint32_t start_nonce, uint32_t count, void* d_results, uint32_t* d_result_count, cudaStream_t stream);

namespace dcr {

CudaBackend::CudaBackend() = default;

CudaBackend::~CudaBackend() {
    shutdown();
}

bool CudaBackend::init(int device_id) {
    device_id_ = device_id;
    if (cudaSetDevice(device_id_) != cudaSuccess) return false;
    
    cudaDeviceProp prop;
    cudaGetDeviceProperties(&prop, device_id_);
    dev_name_ = prop.name;

    cudaStreamCreate(&stream_);
    
    cudaMalloc(&d_results_, 16 * sizeof(SolutionKernel));
    cudaMalloc(&d_result_count_, sizeof(uint32_t));
    
    cudaMallocHost(&h_results_, 16 * sizeof(SolutionKernel));
    cudaMallocHost(&h_result_count_, sizeof(uint32_t));
    
    return true;
}

void CudaBackend::set_job(const ChainValue& midstate, const MsgBlock& block2, const Target256& target) {
    midstate_ = midstate;
    block2_ = block2;
    target_ = target;
}

uint64_t CudaBackend::mine(uint32_t start_nonce, uint32_t count) {
    *h_result_count_ = 0;
    cudaMemcpyAsync(d_result_count_, h_result_count_, sizeof(uint32_t), cudaMemcpyHostToDevice, stream_);
    
    launch_cuda_kernel(midstate_.data(), block2_.data(), target_.data(), start_nonce, count, d_results_, d_result_count_, stream_);
    
    cudaMemcpyAsync(h_result_count_, d_result_count_, sizeof(uint32_t), cudaMemcpyDeviceToHost, stream_);
    cudaMemcpyAsync(h_results_, d_results_, 16 * sizeof(SolutionKernel), cudaMemcpyDeviceToHost, stream_);
    cudaStreamSynchronize(stream_);
    
    return count;
}

std::vector<Solution> CudaBackend::get_solutions() {
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

void CudaBackend::shutdown() {
    if (stream_) {
        cudaStreamDestroy(stream_);
        stream_ = nullptr;
    }
    if (d_results_) { cudaFree(d_results_); d_results_ = nullptr; }
    if (d_result_count_) { cudaFree(d_result_count_); d_result_count_ = nullptr; }
    if (h_results_) { cudaFreeHost(h_results_); h_results_ = nullptr; }
    if (h_result_count_) { cudaFreeHost(h_result_count_); h_result_count_ = nullptr; }
}

std::string CudaBackend::device_name() const {
    return dev_name_;
}

} // namespace dcr

#endif
