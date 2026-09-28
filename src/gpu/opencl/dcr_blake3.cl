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

__kernel void dcr_blake3_mine(__constant uint* midstate, __constant uint* block2, uint start_nonce, uint count, __constant uint* target, __global SolutionKernel* results, volatile __global uint* result_count) {
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

    // R0
    G(v0, v4, v8,  v12, m[0], m[1]);
    G(v1, v5, v9,  v13, m[2], m[3]);
    G(v2, v6, v10, v14, m[4], m[5]);
    G(v3, v7, v11, v15, m[6], m[7]);
    G(v0, v5, v10, v15, m[8], m[9]);
    G(v1, v6, v11, v12, m[10], m[11]);
    G(v2, v7, v8,  v13, m[12], 0);
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
