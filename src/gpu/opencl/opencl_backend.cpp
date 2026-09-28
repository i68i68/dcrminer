#include "opencl_backend.hpp"

#if DCR_ENABLE_OPENCL
#include <iostream>
#include <sstream>
#include <vector>
#include <cstring>

namespace dcr {

static const char* OPENCL_BLAKE3_SRC = R"CLC(
typedef struct {
    uint nonce;
    uint hash[8];
} SolutionKernel;

#define G(a, b, c, d, x, y) \
    a = a + b + x; \
    d = rotate(d ^ a, 16U); \
    c = c + d; \
    b = rotate(b ^ c, 20U); \
    a = a + b + y; \
    d = rotate(d ^ a, 24U); \
    c = c + d; \
    b = rotate(b ^ c, 25U);

__kernel void dcr_blake3_mine(
    __constant uint* midstate,
    __constant uint* block2,
    uint start_nonce,
    uint count,
    __constant uint* target,
    __global SolutionKernel* results,
    volatile __global uint* result_count)
{
    uint thread_id = get_global_id(0);
    if (thread_id >= count) return;
    uint nonce = start_nonce + thread_id;

    uint m[16];
    for (int i = 0; i < 16; ++i) m[i] = block2[i];
    m[3] = nonce;

    uint v0 = midstate[0];
    uint v1 = midstate[1];
    uint v2 = midstate[2];
    uint v3 = midstate[3];
    uint v4 = midstate[4];
    uint v5 = midstate[5];
    uint v6 = midstate[6];
    uint v7 = midstate[7];
    uint v8 = 0x6A09E667;
    uint v9 = 0xBB67AE85;
    uint v10 = 0x3C6EF372;
    uint v11 = 0xA54FF53A;
    uint v12 = 0;
    uint v13 = 0;
    uint v14 = 52;
    uint v15 = 0x0A;

    // Round 0
    G(v0, v4, v8,  v12, m[0], m[1]);
    G(v1, v5, v9,  v13, m[2], m[3]);
    G(v2, v6, v10, v14, m[4], m[5]);
    G(v3, v7, v11, v15, m[6], m[7]);
    G(v0, v5, v10, v15, m[8], m[9]);
    G(v1, v6, v11, v12, m[10], m[11]);
    G(v2, v7, v8,  v13, m[12], 0);
    G(v3, v4, v9,  v14, 0, 0);

    // Round 1
    G(v0, v4, v8,  v12, m[2], m[6]);
    G(v1, v5, v9,  v13, m[3], m[10]);
    G(v2, v6, v10, v14, m[7], m[0]);
    G(v3, v7, v11, v15, m[4], 0);
    G(v0, v5, v10, v15, m[1], m[11]);
    G(v1, v6, v11, v12, m[12], m[5]);
    G(v2, v7, v8,  v13, m[9], 0);
    G(v3, v4, v9,  v14, 0, m[8]);

    // Round 2
    G(v0, v4, v8,  v12, m[3], m[4]);
    G(v1, v5, v9,  v13, m[10], m[12]);
    G(v2, v6, v10, v14, 0, m[2]);
    G(v3, v7, v11, v15, m[7], 0);
    G(v0, v5, v10, v15, m[6], m[5]);
    G(v1, v6, v11, v12, m[9], m[0]);
    G(v2, v7, v8,  v13, m[11], 0);
    G(v3, v4, v9,  v14, m[8], m[1]);

    // Round 3
    G(v0, v4, v8,  v12, m[10], m[7]);
    G(v1, v5, v9,  v13, m[12], m[9]);
    G(v2, v6, v10, v14, 0, m[3]);
    G(v3, v7, v11, v15, 0, 0);
    G(v0, v5, v10, v15, m[4], m[0]);
    G(v1, v6, v11, v12, m[11], m[2]);
    G(v2, v7, v8,  v13, m[5], m[8]);
    G(v3, v4, v9,  v14, m[1], m[6]);

    // Round 4
    G(v0, v4, v8,  v12, m[12], 0);
    G(v1, v5, v9,  v13, m[9], m[11]);
    G(v2, v6, v10, v14, 0, m[10]);
    G(v3, v7, v11, v15, 0, m[8]);
    G(v0, v5, v10, v15, m[7], m[2]);
    G(v1, v6, v11, v12, m[5], m[3]);
    G(v2, v7, v8,  v13, m[0], m[1]);
    G(v3, v4, v9,  v14, m[6], m[4]);

    // Round 5
    G(v0, v4, v8,  v12, m[9], 0);
    G(v1, v5, v9,  v13, m[11], m[5]);
    G(v2, v6, v10, v14, m[8], m[12]);
    G(v3, v7, v11, v15, 0, m[1]);
    G(v0, v5, v10, v15, 0, m[3]);
    G(v1, v6, v11, v12, m[0], m[10]);
    G(v2, v7, v8,  v13, m[2], m[6]);
    G(v3, v4, v9,  v14, m[4], m[7]);

    // Round 6
    G(v0, v4, v8,  v12, m[11], 0);
    G(v1, v5, v9,  v13, m[5], m[0]);
    G(v2, v6, v10, v14, m[1], m[9]);
    G(v3, v7, v11, v15, m[8], m[6]);
    G(v0, v5, v10, v15, 0, m[10]);
    G(v1, v6, v11, v12, m[2], m[12]);
    G(v2, v7, v8,  v13, m[3], m[4]);
    G(v3, v4, v9,  v14, m[7], 0);

    uint h[8];
    h[0] = v0 ^ v8;
    h[1] = v1 ^ v9;
    h[2] = v2 ^ v10;
    h[3] = v3 ^ v11;
    h[4] = v4 ^ v12;
    h[5] = v5 ^ v13;
    h[6] = v6 ^ v14;
    h[7] = v7 ^ v15;

    if (h[7] <= target[7]) {
        bool meets = true;
        for (int i = 7; i >= 0; --i) {
            if (h[i] > target[i]) {
                meets = false;
                break;
            } else if (h[i] < target[i]) {
                break;
            }
        }
        if (meets) {
            uint idx = atomic_inc(result_count);
            if (idx < 16) {
                results[idx].nonce = nonce;
                for (int i = 0; i < 8; ++i) results[idx].hash[i] = h[i];
            }
        }
    }
}
)CLC";

std::vector<OpenCLDeviceEntry> OpenCLBackend::enumerate_devices() {
    std::vector<OpenCLDeviceEntry> list;
    cl_uint num_platforms = 0;
    if (clGetPlatformIDs(0, nullptr, &num_platforms) != CL_SUCCESS || num_platforms == 0) {
        return list;
    }

    std::vector<cl_platform_id> platforms(num_platforms);
    clGetPlatformIDs(num_platforms, platforms.data(), nullptr);

    int global_idx = 0;
    for (auto plat : platforms) {
        char pbuf[256];
        clGetPlatformInfo(plat, CL_PLATFORM_NAME, sizeof(pbuf), pbuf, nullptr);
        std::string plat_name = pbuf;

        cl_uint num_devs = 0;
        if (clGetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, 0, nullptr, &num_devs) != CL_SUCCESS || num_devs == 0) {
            // Also check CL_DEVICE_TYPE_ALL if no explicit GPU
            clGetDeviceIDs(plat, CL_DEVICE_TYPE_ALL, 0, nullptr, &num_devs);
        }
        if (num_devs == 0) continue;

        std::vector<cl_device_id> devs(num_devs);
        clGetDeviceIDs(plat, CL_DEVICE_TYPE_ALL, num_devs, devs.data(), nullptr);

        for (auto dev : devs) {
            char dbuf[256];
            clGetDeviceInfo(dev, CL_DEVICE_NAME, sizeof(dbuf), dbuf, nullptr);
            cl_ulong gmem = 0;
            clGetDeviceInfo(dev, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(gmem), &gmem, nullptr);
            cl_uint cus = 0;
            clGetDeviceInfo(dev, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cus), &cus, nullptr);

            OpenCLDeviceEntry entry;
            entry.global_index = global_idx++;
            entry.platform = plat;
            entry.device = dev;
            entry.platform_name = plat_name;
            entry.device_name = dbuf;
            entry.global_mem_bytes = gmem;
            entry.compute_units = cus;
            list.push_back(entry);
        }
    }
    return list;
}

OpenCLBackend::OpenCLBackend() = default;

OpenCLBackend::~OpenCLBackend() {
    shutdown();
}

bool OpenCLBackend::init(int device_id) {
    auto devices = enumerate_devices();
    if (device_id < 0 || device_id >= static_cast<int>(devices.size())) {
        std::cerr << "[OpenCL ERROR] Invalid device ID " << device_id 
                  << " (Available devices: " << devices.size() << ")\n";
        return false;
    }

    const auto& info = devices[device_id];
    device_id_ = device_id;
    platform_ = info.platform;
    device_ = info.device;
    dev_name_ = info.device_name + " [" + info.platform_name + "]";

    cl_int err = CL_SUCCESS;
    cl_context_properties props[] = {
        CL_CONTEXT_PLATFORM, (cl_context_properties)platform_, 0
    };
    context_ = clCreateContext(props, 1, &device_, nullptr, nullptr, &err);
    if (err != CL_SUCCESS || !context_) {
        std::cerr << "[OpenCL ERROR] clCreateContext failed: " << err << "\n";
        return false;
    }

#if defined(CL_VERSION_2_0)
    queue_ = clCreateCommandQueueWithProperties(context_, device_, nullptr, &err);
    if (err != CL_SUCCESS || !queue_) {
        queue_ = clCreateCommandQueue(context_, device_, 0, &err);
    }
#else
    queue_ = clCreateCommandQueue(context_, device_, 0, &err);
#endif
    if (err != CL_SUCCESS || !queue_) {
        std::cerr << "[OpenCL ERROR] clCreateCommandQueue failed: " << err << "\n";
        shutdown();
        return false;
    }

    size_t src_len = std::strlen(OPENCL_BLAKE3_SRC);
    program_ = clCreateProgramWithSource(context_, 1, &OPENCL_BLAKE3_SRC, &src_len, &err);
    if (err != CL_SUCCESS || !program_) {
        std::cerr << "[OpenCL ERROR] clCreateProgramWithSource failed: " << err << "\n";
        shutdown();
        return false;
    }

    const char* build_opts = "-cl-fast-relaxed-math -cl-mad-enable";
    err = clBuildProgram(program_, 1, &device_, build_opts, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t log_size = 0;
        clGetProgramBuildInfo(program_, device_, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
        std::vector<char> log(log_size + 1);
        clGetProgramBuildInfo(program_, device_, CL_PROGRAM_BUILD_LOG, log_size, log.data(), nullptr);
        std::cerr << "[OpenCL ERROR] clBuildProgram failed (" << err << "):\n" << log.data() << "\n";
        shutdown();
        return false;
    }

    kernel_ = clCreateKernel(program_, "dcr_blake3_mine", &err);
    if (err != CL_SUCCESS || !kernel_) {
        std::cerr << "[OpenCL ERROR] clCreateKernel failed: " << err << "\n";
        shutdown();
        return false;
    }

    // Allocate memory buffers
    d_midstate_ = clCreateBuffer(context_, CL_MEM_READ_ONLY, 8 * sizeof(uint32_t), nullptr, &err);
    d_block2_   = clCreateBuffer(context_, CL_MEM_READ_ONLY, 16 * sizeof(uint32_t), nullptr, &err);
    d_target_   = clCreateBuffer(context_, CL_MEM_READ_ONLY, 8 * sizeof(uint32_t), nullptr, &err);
    d_results_  = clCreateBuffer(context_, CL_MEM_READ_WRITE, 16 * sizeof(SolutionKernel), nullptr, &err);
    d_result_count_ = clCreateBuffer(context_, CL_MEM_READ_WRITE, sizeof(uint32_t), nullptr, &err);

    if (!d_midstate_ || !d_block2_ || !d_target_ || !d_results_ || !d_result_count_) {
        std::cerr << "[OpenCL ERROR] Failed to allocate device buffers\n";
        shutdown();
        return false;
    }

    return true;
}

void OpenCLBackend::set_job(const ChainValue& midstate, const MsgBlock& block2, const Target256& target) {
    midstate_ = midstate;
    block2_ = block2;
    target_ = target;

    clEnqueueWriteBuffer(queue_, d_midstate_, CL_TRUE, 0, 8 * sizeof(uint32_t), midstate_.data(), 0, nullptr, nullptr);
    clEnqueueWriteBuffer(queue_, d_block2_,   CL_TRUE, 0, 16 * sizeof(uint32_t), block2_.data(), 0, nullptr, nullptr);
    clEnqueueWriteBuffer(queue_, d_target_,   CL_TRUE, 0, 8 * sizeof(uint32_t), target_.data(), 0, nullptr, nullptr);
}

uint64_t OpenCLBackend::mine(uint32_t start_nonce, uint32_t count) {
    uint32_t zero = 0;
    clEnqueueWriteBuffer(queue_, d_result_count_, CL_FALSE, 0, sizeof(uint32_t), &zero, 0, nullptr, nullptr);

    clSetKernelArg(kernel_, 0, sizeof(cl_mem), &d_midstate_);
    clSetKernelArg(kernel_, 1, sizeof(cl_mem), &d_block2_);
    clSetKernelArg(kernel_, 2, sizeof(uint32_t), &start_nonce);
    clSetKernelArg(kernel_, 3, sizeof(uint32_t), &count);
    clSetKernelArg(kernel_, 4, sizeof(cl_mem), &d_target_);
    clSetKernelArg(kernel_, 5, sizeof(cl_mem), &d_results_);
    clSetKernelArg(kernel_, 6, sizeof(cl_mem), &d_result_count_);

    size_t max_wg = 256;
    if (clGetKernelWorkGroupInfo(kernel_, device_, CL_KERNEL_WORK_GROUP_SIZE, sizeof(max_wg), &max_wg, nullptr) != CL_SUCCESS || max_wg == 0) {
        max_wg = 256;
    }
    size_t local_size = std::min<size_t>(256, max_wg);
    if (local_size == 0) local_size = 64;
    size_t global_size = ((count + local_size - 1) / local_size) * local_size;

    clEnqueueNDRangeKernel(queue_, kernel_, 1, nullptr, &global_size, &local_size, 0, nullptr, nullptr);

    clEnqueueReadBuffer(queue_, d_result_count_, CL_FALSE, 0, sizeof(uint32_t), &h_result_count_, 0, nullptr, nullptr);
    clEnqueueReadBuffer(queue_, d_results_, CL_FALSE, 0, 16 * sizeof(SolutionKernel), h_results_, 0, nullptr, nullptr);
    clFinish(queue_);

    return count;
}

std::vector<Solution> OpenCLBackend::get_solutions() {
    std::vector<Solution> res;
    uint32_t c = h_result_count_;
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

void OpenCLBackend::shutdown() {
    if (d_result_count_) { clReleaseMemObject(d_result_count_); d_result_count_ = nullptr; }
    if (d_results_)      { clReleaseMemObject(d_results_); d_results_ = nullptr; }
    if (d_target_)       { clReleaseMemObject(d_target_); d_target_ = nullptr; }
    if (d_block2_)       { clReleaseMemObject(d_block2_); d_block2_ = nullptr; }
    if (d_midstate_)     { clReleaseMemObject(d_midstate_); d_midstate_ = nullptr; }

    if (kernel_)         { clReleaseKernel(kernel_); kernel_ = nullptr; }
    if (program_)        { clReleaseProgram(program_); program_ = nullptr; }
    if (queue_)          { clReleaseCommandQueue(queue_); queue_ = nullptr; }
    if (context_)        { clReleaseContext(context_); context_ = nullptr; }
}

std::string OpenCLBackend::device_name() const {
    return dev_name_;
}

} // namespace dcr

#endif
