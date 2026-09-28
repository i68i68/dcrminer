#ifndef DCR_ENABLE_CUDA
#define DCR_ENABLE_CUDA 1
#endif
#if DCR_ENABLE_CUDA

#include <cuda_runtime.h>
#include <stdint.h>

__constant__ uint32_t c_midstate[8];
__constant__ uint32_t c_block2[16];
__constant__ uint32_t c_target[8];

#define ROR32(x, y) (((x) >> (y)) | ((x) << (32 - (y))))

// Use __byte_perm for 8 and 16 bit rotations, and __funnelshift_r for 12 and 7 bit rotations (single-cycle hardware rotate)
__device__ __forceinline__ uint32_t rotr16(uint32_t x) { return __byte_perm(x, x, 0x1032); }
__device__ __forceinline__ uint32_t rotr8(uint32_t x)  { return __byte_perm(x, x, 0x0321); }
__device__ __forceinline__ uint32_t rotr12(uint32_t x) { return __funnelshift_r(x, x, 12); }
__device__ __forceinline__ uint32_t rotr7(uint32_t x)  { return __funnelshift_r(x, x, 7); }

#define G(a, b, c, d, x, y) \
    a = a + b + x; \
    d = rotr16(d ^ a); \
    c = c + d; \
    b = rotr12(b ^ c); \
    a = a + b + y; \
    d = rotr8(d ^ a); \
    c = c + d; \
    b = rotr7(b ^ c);

#define G_ZERO(a, b, c, d, x) \
    a = a + b + x; \
    d = rotr16(d ^ a); \
    c = c + d; \
    b = rotr12(b ^ c); \
    a = a + b; \
    d = rotr8(d ^ a); \
    c = c + d; \
    b = rotr7(b ^ c);

#define ROUND(r, m, s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10,s11,s12,s13,s14,s15) \
    G(v0, v4, v8,  v12, m[s0], m[s1]); \
    G(v1, v5, v9,  v13, m[s2], m[s3]); \
    G(v2, v6, v10, v14, m[s4], m[s5]); \
    G(v3, v7, v11, v15, m[s6], m[s7]); \
    G(v0, v5, v10, v15, m[s8], m[s9]); \
    G(v1, v6, v11, v12, m[s10], m[s11]); \
    G(v2, v7, v8,  v13, m[s12], m[s13]); \
    G(v3, v4, v9,  v14, m[s14], m[s15]);

struct SolutionKernel {
    uint32_t nonce;
    uint32_t hash[8];
};

__global__ void dcr_blake3_mine(uint32_t start_nonce, uint32_t count, SolutionKernel* results, uint32_t* result_count) {
    uint32_t thread_id = blockIdx.x * blockDim.x + threadIdx.x;
    if (thread_id >= count) return;
    uint32_t nonce = start_nonce + thread_id;

    uint32_t m[16];
    #pragma unroll
    for (int i = 0; i < 16; ++i) m[i] = c_block2[i];
    m[3] = nonce;

    uint32_t v0 = c_midstate[0];
    uint32_t v1 = c_midstate[1];
    uint32_t v2 = c_midstate[2];
    uint32_t v3 = c_midstate[3];
    uint32_t v4 = c_midstate[4];
    uint32_t v5 = c_midstate[5];
    uint32_t v6 = c_midstate[6];
    uint32_t v7 = c_midstate[7];
    uint32_t v8 = 0x6A09E667;
    uint32_t v9 = 0xBB67AE85;
    uint32_t v10 = 0x3C6EF372;
    uint32_t v11 = 0xA54FF53A;
    uint32_t v12 = 0;
    uint32_t v13 = 0;
    uint32_t v14 = 52;
    uint32_t v15 = 0x0A;

    // R0
    G(v0, v4, v8,  v12, m[0], m[1]);
    G(v1, v5, v9,  v13, m[2], m[3]);
    G(v2, v6, v10, v14, m[4], m[5]);
    G(v3, v7, v11, v15, m[6], m[7]);
    G(v0, v5, v10, v15, m[8], m[9]);
    G(v1, v6, v11, v12, m[10], m[11]);
    // m[13] is 0
    G(v2, v7, v8,  v13, m[12], 0);
    // m[14], m[15] are 0
    G(v3, v4, v9,  v14, 0, 0);

    // R1
    G(v0, v4, v8,  v12, m[2], m[6]);
    G(v1, v5, v9,  v13, m[3], m[10]);
    G(v2, v6, v10, v14, m[7], m[0]);
    G(v3, v7, v11, v15, m[4], 0);
    G(v0, v5, v10, v15, m[1], m[11]);
    G(v1, v6, v11, v12, m[12], m[5]);
    G(v2, v7, v8,  v13, m[9], 0);
    G(v3, v4, v9,  v14, 0, m[8]);
    
    // R2
    G(v0, v4, v8,  v12, m[3], m[4]);
    G(v1, v5, v9,  v13, m[10], m[12]);
    G(v2, v6, v10, v14, 0, m[2]);
    G(v3, v7, v11, v15, m[7], 0);
    G(v0, v5, v10, v15, m[6], m[5]);
    G(v1, v6, v11, v12, m[9], m[0]);
    G(v2, v7, v8,  v13, m[11], 0);
    G(v3, v4, v9,  v14, m[8], m[1]);

    // R3
    G(v0, v4, v8,  v12, m[10], m[7]);
    G(v1, v5, v9,  v13, m[12], m[9]);
    G(v2, v6, v10, v14, 0, m[3]);
    G(v3, v7, v11, v15, 0, 0);
    G(v0, v5, v10, v15, m[4], m[0]);
    G(v1, v6, v11, v12, m[11], m[2]);
    G(v2, v7, v8,  v13, m[5], m[8]);
    G(v3, v4, v9,  v14, m[1], m[6]);

    // R4
    G(v0, v4, v8,  v12, m[12], 0);
    G(v1, v5, v9,  v13, m[9], m[11]);
    G(v2, v6, v10, v14, 0, m[10]);
    G(v3, v7, v11, v15, 0, m[8]);
    G(v0, v5, v10, v15, m[7], m[2]);
    G(v1, v6, v11, v12, m[5], m[3]);
    G(v2, v7, v8,  v13, m[0], m[1]);
    G(v3, v4, v9,  v14, m[6], m[4]);

    // R5
    G(v0, v4, v8,  v12, m[9], 0);
    G(v1, v5, v9,  v13, m[11], m[5]);
    G(v2, v6, v10, v14, m[8], m[12]);
    G(v3, v7, v11, v15, 0, m[1]);
    G(v0, v5, v10, v15, 0, m[3]);
    G(v1, v6, v11, v12, m[0], m[10]);
    G(v2, v7, v8,  v13, m[2], m[6]);
    G(v3, v4, v9,  v14, m[4], m[7]);

    // R6
    G(v0, v4, v8,  v12, m[11], 0);
    G(v1, v5, v9,  v13, m[5], m[0]);
    G(v2, v6, v10, v14, m[1], m[9]);
    G(v3, v7, v11, v15, m[8], m[6]);
    G(v0, v5, v10, v15, 0, m[10]);
    G(v1, v6, v11, v12, m[2], m[12]);
    G(v2, v7, v8,  v13, m[3], m[4]);
    G(v3, v4, v9,  v14, m[7], 0);

    uint32_t h[8];
    h[0] = v0 ^ v8;
    h[1] = v1 ^ v9;
    h[2] = v2 ^ v10;
    h[3] = v3 ^ v11;
    h[4] = v4 ^ v12;
    h[5] = v5 ^ v13;
    h[6] = v6 ^ v14;
    h[7] = v7 ^ v15;

    bool meets = false;
    if (h[7] <= c_target[7]) {
        meets = true;
        for (int i = 7; i >= 0; --i) {
            if (h[i] > c_target[i]) {
                meets = false;
                break;
            } else if (h[i] < c_target[i]) {
                break;
            }
        }
    }

    // Warp ballot pruning: 99.9999% of warps will evaluate mask == 0 and bypass atomicAdd completely
    unsigned int active = __activemask();
    unsigned int warp_mask = __ballot_sync(active, meets ? 1 : 0);
    if (warp_mask != 0 && meets) {
        uint32_t idx = atomicAdd(result_count, 1);
        if (idx < 16) {
            results[idx].nonce = nonce;
            for (int i = 0; i < 8; ++i) results[idx].hash[i] = h[i];
        }
    }
}

extern "C" void launch_cuda_kernel(const uint32_t* midstate, const uint32_t* block2, const uint32_t* target, uint32_t start_nonce, uint32_t count, SolutionKernel* d_results, uint32_t* d_result_count, cudaStream_t stream) {
    cudaMemcpyToSymbolAsync(c_midstate, midstate, 8 * sizeof(uint32_t), 0, cudaMemcpyHostToDevice, stream);
    cudaMemcpyToSymbolAsync(c_block2, block2, 16 * sizeof(uint32_t), 0, cudaMemcpyHostToDevice, stream);
    cudaMemcpyToSymbolAsync(c_target, target, 8 * sizeof(uint32_t), 0, cudaMemcpyHostToDevice, stream);
    
    int block_size = 256;
    int num_blocks = (count + block_size - 1) / block_size;
    
    dcr_blake3_mine<<<num_blocks, block_size, 0, stream>>>(start_nonce, count, d_results, d_result_count);
}
#endif
