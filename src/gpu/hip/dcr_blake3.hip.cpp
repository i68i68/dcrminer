#ifndef DCR_ENABLE_HIP
#define DCR_ENABLE_HIP 1
#endif
#if DCR_ENABLE_HIP

#include <hip/hip_runtime.h>
#include <stdint.h>

__constant__ uint32_t h_midstate[8];
__constant__ uint32_t h_block2[16];
__constant__ uint32_t h_target[8];

__device__ __forceinline__ uint32_t rotr16(uint32_t x) { return (x >> 16) | (x << 16); }
__device__ __forceinline__ uint32_t rotr8(uint32_t x) { return (x >> 8) | (x << 24); }
__device__ __forceinline__ uint32_t rotr12(uint32_t x) { return (x >> 12) | (x << 20); }
__device__ __forceinline__ uint32_t rotr7(uint32_t x) { return (x >> 7) | (x << 25); }

#define G(a, b, c, d, x, y) \
    a = a + b + x; \
    d = rotr16(d ^ a); \
    c = c + d; \
    b = rotr12(b ^ c); \
    a = a + b + y; \
    d = rotr8(d ^ a); \
    c = c + d; \
    b = rotr7(b ^ c);

struct SolutionKernel {
    uint32_t nonce;
    uint32_t hash[8];
};

__global__ void dcr_blake3_mine_hip(uint32_t start_nonce, uint32_t count, SolutionKernel* results, uint32_t* result_count) {
    uint32_t thread_id = blockIdx.x * blockDim.x + threadIdx.x;
    if (thread_id >= count) return;
    uint32_t nonce = start_nonce + thread_id;

    const uint32_t m0  = h_block2[0];
    const uint32_t m1  = h_block2[1];
    const uint32_t m2  = h_block2[2];
    const uint32_t m3  = nonce;
    const uint32_t m4  = h_block2[4];
    const uint32_t m5  = h_block2[5];
    const uint32_t m6  = h_block2[6];
    const uint32_t m7  = h_block2[7];
    const uint32_t m8  = h_block2[8];
    const uint32_t m9  = h_block2[9];
    const uint32_t m10 = h_block2[10];
    const uint32_t m11 = h_block2[11];
    const uint32_t m12 = h_block2[12];

    uint32_t v0 = h_midstate[0];
    uint32_t v1 = h_midstate[1];
    uint32_t v2 = h_midstate[2];
    uint32_t v3 = h_midstate[3];
    uint32_t v4 = h_midstate[4];
    uint32_t v5 = h_midstate[5];
    uint32_t v6 = h_midstate[6];
    uint32_t v7 = h_midstate[7];
    uint32_t v8 = 0x6A09E667;
    uint32_t v9 = 0xBB67AE85;
    uint32_t v10 = 0x3C6EF372;
    uint32_t v11 = 0xA54FF53A;
    uint32_t v12 = 0;
    uint32_t v13 = 0;
    uint32_t v14 = 52;
    uint32_t v15 = 0x0A;

    // R0
    G(v0, v4, v8,  v12, m0,  m1);
    G(v1, v5, v9,  v13, m2,  m3);
    G(v2, v6, v10, v14, m4,  m5);
    G(v3, v7, v11, v15, m6,  m7);
    G(v0, v5, v10, v15, m8,  m9);
    G(v1, v6, v11, v12, m10, m11);
    G(v2, v7, v8,  v13, m12, 0);
    G(v3, v4, v9,  v14, 0,   0);

    // R1
    G(v0, v4, v8,  v12, m2,  m6);
    G(v1, v5, v9,  v13, m3,  m10);
    G(v2, v6, v10, v14, m7,  m0);
    G(v3, v7, v11, v15, m4,  0);
    G(v0, v5, v10, v15, m1,  m11);
    G(v1, v6, v11, v12, m12, m5);
    G(v2, v7, v8,  v13, m9,  0);
    G(v3, v4, v9,  v14, 0,   m8);
    
    // R2
    G(v0, v4, v8,  v12, m3,  m4);
    G(v1, v5, v9,  v13, m10, m12);
    G(v2, v6, v10, v14, 0,   m2);
    G(v3, v7, v11, v15, m7,  0);
    G(v0, v5, v10, v15, m6,  m5);
    G(v1, v6, v11, v12, m9,  m0);
    G(v2, v7, v8,  v13, m11, 0);
    G(v3, v4, v9,  v14, m8,  m1);

    // R3
    G(v0, v4, v8,  v12, m10, m7);
    G(v1, v5, v9,  v13, m12, m9);
    G(v2, v6, v10, v14, 0,   m3);
    G(v3, v7, v11, v15, 0,   0);
    G(v0, v5, v10, v15, m4,  m0);
    G(v1, v6, v11, v12, m11, m2);
    G(v2, v7, v8,  v13, m5,  m8);
    G(v3, v4, v9,  v14, m1,  m6);

    // R4
    G(v0, v4, v8,  v12, m12, 0);
    G(v1, v5, v9,  v13, m9,  m11);
    G(v2, v6, v10, v14, 0,   m10);
    G(v3, v7, v11, v15, 0,   m8);
    G(v0, v5, v10, v15, m7,  m2);
    G(v1, v6, v11, v12, m5,  m3);
    G(v2, v7, v8,  v13, m0,  m1);
    G(v3, v4, v9,  v14, m6,  m4);

    // R5
    G(v0, v4, v8,  v12, m9,  0);
    G(v1, v5, v9,  v13, m11, m5);
    G(v2, v6, v10, v14, m8,  m12);
    G(v3, v7, v11, v15, 0,   m1);
    G(v0, v5, v10, v15, 0,   m3);
    G(v1, v6, v11, v12, m0,  m10);
    G(v2, v7, v8,  v13, m2,  m6);
    G(v3, v4, v9,  v14, m4,  m7);

    // R6
    G(v0, v4, v8,  v12, m11, 0);
    G(v1, v5, v9,  v13, m5,  m0);
    G(v2, v6, v10, v14, m1,  m9);
    G(v3, v7, v11, v15, m8,  m6);
    G(v0, v5, v10, v15, 0,   m10);
    G(v1, v6, v11, v12, m2,  m12);
    G(v2, v7, v8,  v13, m3,  m4);
    G(v3, v4, v9,  v14, m7,  0);

    uint32_t h[8];
    h[0] = v0 ^ v8;
    h[1] = v1 ^ v9;
    h[2] = v2 ^ v10;
    h[3] = v3 ^ v11;
    h[4] = v4 ^ v12;
    h[5] = v5 ^ v13;
    h[6] = v6 ^ v14;
    h[7] = v7 ^ v15;

    if (h[7] <= h_target[7]) {
        bool meets = true;
        for (int i = 7; i >= 0; --i) {
            if (h[i] > h_target[i]) {
                meets = false;
                break;
            } else if (h[i] < h_target[i]) {
                break;
            }
        }
        if (meets) {
            uint32_t idx = atomicAdd(result_count, 1);
            if (idx < 16) {
                results[idx].nonce = nonce;
                for (int i = 0; i < 8; ++i) results[idx].hash[i] = h[i];
            }
        }
    }
}

extern "C" void launch_hip_kernel(const uint32_t* midstate, const uint32_t* block2, const uint32_t* target, uint32_t start_nonce, uint32_t count, SolutionKernel* d_results, uint32_t* d_result_count, hipStream_t stream) {
    hipMemcpyToSymbol(HIP_SYMBOL(h_midstate), midstate, 8 * sizeof(uint32_t), 0, hipMemcpyHostToDevice);
    hipMemcpyToSymbol(HIP_SYMBOL(h_block2), block2, 16 * sizeof(uint32_t), 0, hipMemcpyHostToDevice);
    hipMemcpyToSymbol(HIP_SYMBOL(h_target), target, 8 * sizeof(uint32_t), 0, hipMemcpyHostToDevice);
    
    int block_size = 256;
    int num_blocks = (count + block_size - 1) / block_size;
    
    hipLaunchKernelGGL(dcr_blake3_mine_hip, dim3(num_blocks), dim3(block_size), 0, stream, start_nonce, count, d_results, d_result_count);
}
#endif
