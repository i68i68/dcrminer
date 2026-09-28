#pragma once
#include "../gpu_backend.hpp"

#if DCR_ENABLE_OPENCL
#include <CL/cl.h>
#include <string>
#include <vector>

namespace dcr {

struct OpenCLDeviceEntry {
    int global_index;
    cl_platform_id platform;
    cl_device_id device;
    std::string platform_name;
    std::string device_name;
    uint64_t global_mem_bytes;
    uint32_t compute_units;
};

class OpenCLBackend : public GpuBackend {
public:
    OpenCLBackend();
    ~OpenCLBackend() override;

    std::string name() const override { return "OpenCL"; }
    BackendType type() const override { return BackendType::OPENCL; }
    bool init(int device_id) override;
    void set_job(const ChainValue& midstate, const MsgBlock& block2, const Target256& target) override;
    uint64_t mine(uint32_t start_nonce, uint32_t count) override;
    std::vector<Solution> get_solutions() override;
    void shutdown() override;
    std::string device_name() const override;

    static std::vector<OpenCLDeviceEntry> enumerate_devices();

private:
    struct SolutionKernel {
        uint32_t nonce;
        uint32_t hash[8];
    };

    int device_id_ = -1;
    cl_platform_id platform_ = nullptr;
    cl_device_id device_ = nullptr;
    cl_context context_ = nullptr;
    cl_command_queue queue_ = nullptr;
    cl_program program_ = nullptr;
    cl_kernel kernel_ = nullptr;

    cl_mem d_midstate_ = nullptr;
    cl_mem d_block2_ = nullptr;
    cl_mem d_target_ = nullptr;
    cl_mem d_results_ = nullptr;
    cl_mem d_result_count_ = nullptr;

    SolutionKernel h_results_[16]{};
    uint32_t h_result_count_ = 0;

    ChainValue midstate_{};
    MsgBlock block2_{};
    Target256 target_{};

    std::string dev_name_;
};

} // namespace dcr

#endif
