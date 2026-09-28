#pragma once

#include "dcr_blake3_scalar.hpp" // For Solution struct
#include <optional>

#if defined(__AVX512F__)

#include <immintrin.h>

#if defined(_MSC_VER)
#define DCR_FORCEINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define DCR_FORCEINLINE inline __attribute__((always_inline))
#else
#define DCR_FORCEINLINE inline
#endif

namespace dcr {
namespace blake3 {

// AVX-512 has native bitwise rotation for 32-bit integers
DCR_FORCEINLINE __m512i rotr16_avx512(__m512i x) { return _mm512_ror_epi32(x, 16); }
DCR_FORCEINLINE __m512i rotr12_avx512(__m512i x) { return _mm512_ror_epi32(x, 12); }
DCR_FORCEINLINE __m512i rotr8_avx512(__m512i x)  { return _mm512_ror_epi32(x, 8); }
DCR_FORCEINLINE __m512i rotr7_avx512(__m512i x)  { return _mm512_ror_epi32(x, 7); }

template <bool x_zero, bool y_zero>
DCR_FORCEINLINE void avx512_g_func(__m512i& a, __m512i& b, __m512i& c, __m512i& d, __m512i mx, __m512i my) {
    if constexpr (x_zero) {
        a = _mm512_add_epi32(a, b);
    } else {
        a = _mm512_add_epi32(_mm512_add_epi32(a, b), mx);
    }
    d = rotr16_avx512(_mm512_xor_si512(d, a));
    c = _mm512_add_epi32(c, d);
    b = rotr12_avx512(_mm512_xor_si512(b, c));
    
    if constexpr (y_zero) {
        a = _mm512_add_epi32(a, b);
    } else {
        a = _mm512_add_epi32(_mm512_add_epi32(a, b), my);
    }
    d = rotr8_avx512(_mm512_xor_si512(d, a));
    c = _mm512_add_epi32(c, d);
    b = rotr7_avx512(_mm512_xor_si512(b, c));
}

DCR_FORCEINLINE void avx512_hash_batch(const uint32_t midstate[8], const uint32_t block2_words[16], uint32_t base_nonce, uint32_t out_hashes[8][16]) {
    __m512i v[16];
    for (int i = 0; i < 8; ++i) v[i] = _mm512_set1_epi32(midstate[i]);
    v[8]  = _mm512_set1_epi32(0x6A09E667);
    v[9]  = _mm512_set1_epi32(0xBB67AE85);
    v[10] = _mm512_set1_epi32(0x3C6EF372);
    v[11] = _mm512_set1_epi32(0xA54FF53A);
    v[12] = _mm512_setzero_si512();
    v[13] = _mm512_setzero_si512();
    v[14] = _mm512_set1_epi32(52);
    v[15] = _mm512_set1_epi32(0x0A); // flags

    __m512i m[16];
    for (int i = 0; i < 16; ++i) {
        if (i == 3) {
            m[i] = _mm512_set_epi32(
                base_nonce + 15, base_nonce + 14, base_nonce + 13, base_nonce + 12,
                base_nonce + 11, base_nonce + 10, base_nonce + 9,  base_nonce + 8,
                base_nonce + 7,  base_nonce + 6,  base_nonce + 5,  base_nonce + 4,
                base_nonce + 3,  base_nonce + 2,  base_nonce + 1,  base_nonce + 0
            );
        } else {
            m[i] = _mm512_set1_epi32(block2_words[i]);
        }
    }

#define AVX512_ROUND_I(rnd, s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10,s11,s12,s13,s14,s15) \
    avx512_g_func<(s0>=13), (s1>=13)>(v[0], v[4], v[8], v[12], m[s0], m[s1]); \
    avx512_g_func<(s2>=13), (s3>=13)>(v[1], v[5], v[9], v[13], m[s2], m[s3]); \
    avx512_g_func<(s4>=13), (s5>=13)>(v[2], v[6], v[10], v[14], m[s4], m[s5]); \
    avx512_g_func<(s6>=13), (s7>=13)>(v[3], v[7], v[11], v[15], m[s6], m[s7]); \
    avx512_g_func<(s8>=13), (s9>=13)>(v[0], v[5], v[10], v[15], m[s8], m[s9]); \
    avx512_g_func<(s10>=13), (s11>=13)>(v[1], v[6], v[11], v[12], m[s10], m[s11]); \
    avx512_g_func<(s12>=13), (s13>=13)>(v[2], v[7], v[8], v[13], m[s12], m[s13]); \
    avx512_g_func<(s14>=13), (s15>=13)>(v[3], v[4], v[9], v[14], m[s14], m[s15]);

    AVX512_ROUND_I(0, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15)
    AVX512_ROUND_I(1, 2,6,3,10,7,0,4,13,1,11,12,5,9,14,15,8)
    AVX512_ROUND_I(2, 3,4,10,12,13,2,7,14,6,5,9,0,11,15,8,1)
    AVX512_ROUND_I(3, 10,7,12,9,14,3,13,15,4,0,11,2,5,8,1,6)
    AVX512_ROUND_I(4, 12,13,9,11,15,10,14,8,7,2,5,3,0,1,6,4)
    AVX512_ROUND_I(5, 9,14,11,5,8,12,15,1,13,3,0,10,2,6,4,7)
    AVX512_ROUND_I(6, 11,15,5,0,1,9,8,6,14,10,2,12,3,4,7,13)

#undef AVX512_ROUND_I

    for (int i = 0; i < 8; ++i) {
        __m512i h = _mm512_xor_si512(v[i], v[i + 8]);
        _mm512_storeu_si512((__m512i*)out_hashes[i], h);
    }
}

inline std::optional<Solution> avx512_mine_batch(const uint32_t midstate[8], const uint32_t block2_words[16], uint32_t base_nonce, const uint32_t target[8]) {
    alignas(64) uint32_t hashes[8][16]; // [hash_word][lane]
    avx512_hash_batch(midstate, block2_words, base_nonce, hashes);

    for (int lane = 0; lane < 16; ++lane) {
        bool meets_target = false;
        for (int j = 7; j >= 0; --j) {
            if (hashes[j][lane] < target[j]) {
                meets_target = true;
                break;
            } else if (hashes[j][lane] > target[j]) {
                break;
            }
            if (j == 0) meets_target = true;
        }

        if (meets_target) {
            Solution sol;
            sol.nonce = base_nonce + lane;
            for (int k = 0; k < 8; ++k) {
                sol.hash_words[k] = hashes[k][lane];
                write_le32(sol.pow_hash.data() + k * 4, hashes[k][lane]);
            }
            return sol;
        }
    }

    return std::nullopt;
}

inline std::optional<Solution> avx512_mine_range_impl(const uint32_t midstate[8], const uint32_t block2_words[16], uint32_t start_nonce, uint32_t count, const uint32_t target[8]) {
    for (uint32_t i = 0; i < count; i += 16) {
        auto sol = avx512_mine_batch(midstate, block2_words, start_nonce + i, target);
        if (sol) return sol;
    }
    return std::nullopt;
}

} // namespace blake3
} // namespace dcr

#endif // __AVX512F__
