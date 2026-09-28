#pragma once

#include "../common/types.hpp"
#include "blake3_constants.hpp"
#include <optional>
#include <cstring>

#if defined(_MSC_VER)
#define DCR_FORCEINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define DCR_FORCEINLINE inline __attribute__((always_inline))
#else
#define DCR_FORCEINLINE inline
#endif

namespace dcr {
namespace blake3 {

template <bool x_zero, bool y_zero>
DCR_FORCEINLINE void scalar_g_func(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d, uint32_t mx, uint32_t my) {
    if constexpr (x_zero) {
        a = a + b;
    } else {
        a = a + b + mx;
    }
    d = rotr32(d ^ a, 16);
    c = c + d;
    b = rotr32(b ^ c, 12);
    
    if constexpr (y_zero) {
        a = a + b;
    } else {
        a = a + b + my;
    }
    d = rotr32(d ^ a, 8);
    c = c + d;
    b = rotr32(b ^ c, 7);
}



DCR_FORCEINLINE void scalar_hash_final_block(const uint32_t midstate[8], const uint32_t block2_words[16], uint32_t nonce, uint32_t hash_words[8]) {
    uint32_t v[16];
    for (int i = 0; i < 8; ++i) v[i] = midstate[i];
    v[8]  = 0x6A09E667;
    v[9]  = 0xBB67AE85;
    v[10] = 0x3C6EF372;
    v[11] = 0xA54FF53A;
    v[12] = 0;
    v[13] = 0;
    v[14] = 52;
    v[15] = 0x0A; // flags: ROOT | CHUNK_END | CHUNK_START = 10

    uint32_t m[16];
    for (int i = 0; i < 16; ++i) m[i] = block2_words[i];
    m[3] = nonce;

    // Fully unrolled 7 rounds
    // schedule values are known at compile time but macro unrolling is verbose.
    // Instead, we use template instantiation inside scalar_round (C++20 would allow constexpr loop, but this is fine)
    // Actually, we must manually unroll or let the compiler do it because schedule array elements must be constexpr for template instantiation.
    // Let's manually unroll by providing the schedule as constexpr arrays.
#define SCALAR_ROUND_I(rnd, s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10,s11,s12,s13,s14,s15) \
    scalar_g_func<(s0>=13), (s1>=13)>(v[0], v[4], v[8], v[12], (s0>=13)?0:m[s0], (s1>=13)?0:m[s1]); \
    scalar_g_func<(s2>=13), (s3>=13)>(v[1], v[5], v[9], v[13], (s2>=13)?0:m[s2], (s3>=13)?0:m[s3]); \
    scalar_g_func<(s4>=13), (s5>=13)>(v[2], v[6], v[10], v[14], (s4>=13)?0:m[s4], (s5>=13)?0:m[s5]); \
    scalar_g_func<(s6>=13), (s7>=13)>(v[3], v[7], v[11], v[15], (s6>=13)?0:m[s6], (s7>=13)?0:m[s7]); \
    scalar_g_func<(s8>=13), (s9>=13)>(v[0], v[5], v[10], v[15], (s8>=13)?0:m[s8], (s9>=13)?0:m[s9]); \
    scalar_g_func<(s10>=13), (s11>=13)>(v[1], v[6], v[11], v[12], (s10>=13)?0:m[s10], (s11>=13)?0:m[s11]); \
    scalar_g_func<(s12>=13), (s13>=13)>(v[2], v[7], v[8], v[13], (s12>=13)?0:m[s12], (s13>=13)?0:m[s13]); \
    scalar_g_func<(s14>=13), (s15>=13)>(v[3], v[4], v[9], v[14], (s14>=13)?0:m[s14], (s15>=13)?0:m[s15]);

    SCALAR_ROUND_I(0, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15)
    SCALAR_ROUND_I(1, 2,6,3,10,7,0,4,13,1,11,12,5,9,14,15,8)
    SCALAR_ROUND_I(2, 3,4,10,12,13,2,7,14,6,5,9,0,11,15,8,1)
    SCALAR_ROUND_I(3, 10,7,12,9,14,3,13,15,4,0,11,2,5,8,1,6)
    SCALAR_ROUND_I(4, 12,13,9,11,15,10,14,8,7,2,5,3,0,1,6,4)
    SCALAR_ROUND_I(5, 9,14,11,5,8,12,15,1,13,3,0,10,2,6,4,7)
    SCALAR_ROUND_I(6, 11,15,5,0,1,9,8,6,14,10,2,12,3,4,7,13)

#undef SCALAR_ROUND_I

    for (int i = 0; i < 8; ++i) {
        hash_words[i] = v[i] ^ v[i + 8];
    }
}

inline std::optional<Solution> scalar_mine_range(const uint32_t midstate[8], const uint32_t block2_words[16], uint32_t start_nonce, uint32_t count, const uint32_t target[8]) {
    uint32_t hash_words[8];
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t nonce = start_nonce + i;
        scalar_hash_final_block(midstate, block2_words, nonce, hash_words);
        
        // Compare with target (little endian, comparing from highest word down)
        bool meets_target = false;
        for (int j = 7; j >= 0; --j) {
            if (hash_words[j] < target[j]) {
                meets_target = true;
                break;
            } else if (hash_words[j] > target[j]) {
                break;
            }
            if (j == 0) meets_target = true; // Equal to target
        }
        
        if (meets_target) {
            Solution sol;
            sol.nonce = nonce;
            for (int k = 0; k < 8; ++k) {
                sol.hash_words[k] = hash_words[k];
                write_le32(sol.pow_hash.data() + k * 4, hash_words[k]);
            }
            return sol;
        }
    }
    return std::nullopt;
}

} // namespace blake3
} // namespace dcr
