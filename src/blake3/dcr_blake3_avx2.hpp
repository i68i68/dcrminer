#pragma once

#include "dcr_blake3_scalar.hpp" // For Solution struct
#include <optional>

#if defined(__AVX2__)

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

// AVX2 rotation constants
static const __m256i ROT16_MASK = _mm256_set_epi8(
    13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2,
    13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2
);
static const __m256i ROT8_MASK = _mm256_set_epi8(
    12, 15, 14, 13, 8, 11, 10, 9, 4, 7, 6, 5, 0, 3, 2, 1,
    12, 15, 14, 13, 8, 11, 10, 9, 4, 7, 6, 5, 0, 3, 2, 1
);

DCR_FORCEINLINE __m256i rotr16_avx2(__m256i x) { return _mm256_shuffle_epi8(x, ROT16_MASK); }
DCR_FORCEINLINE __m256i rotr12_avx2(__m256i x) { return _mm256_or_si256(_mm256_srli_epi32(x, 12), _mm256_slli_epi32(x, 20)); }
DCR_FORCEINLINE __m256i rotr8_avx2(__m256i x)  { return _mm256_shuffle_epi8(x, ROT8_MASK); }
DCR_FORCEINLINE __m256i rotr7_avx2(__m256i x)  { return _mm256_or_si256(_mm256_srli_epi32(x, 7), _mm256_slli_epi32(x, 25)); }

template <bool x_zero, bool y_zero>
DCR_FORCEINLINE void avx2_g_func(__m256i& a, __m256i& b, __m256i& c, __m256i& d, __m256i mx, __m256i my) {
    if constexpr (x_zero) {
        a = _mm256_add_epi32(a, b);
    } else {
        a = _mm256_add_epi32(_mm256_add_epi32(a, b), mx);
    }
    d = rotr16_avx2(_mm256_xor_si256(d, a));
    c = _mm256_add_epi32(c, d);
    b = rotr12_avx2(_mm256_xor_si256(b, c));
    
    if constexpr (y_zero) {
        a = _mm256_add_epi32(a, b);
    } else {
        a = _mm256_add_epi32(_mm256_add_epi32(a, b), my);
    }
    d = rotr8_avx2(_mm256_xor_si256(d, a));
    c = _mm256_add_epi32(c, d);
    b = rotr7_avx2(_mm256_xor_si256(b, c));
}

DCR_FORCEINLINE void avx2_hash_batch(const uint32_t midstate[8], const uint32_t block2_words[16], uint32_t base_nonce, uint32_t out_hashes[8][8]) {
    __m256i v[16];
    for (int i = 0; i < 8; ++i) v[i] = _mm256_set1_epi32(midstate[i]);
    v[8]  = _mm256_set1_epi32(0x6A09E667);
    v[9]  = _mm256_set1_epi32(0xBB67AE85);
    v[10] = _mm256_set1_epi32(0x3C6EF372);
    v[11] = _mm256_set1_epi32(0xA54FF53A);
    v[12] = _mm256_setzero_si256();
    v[13] = _mm256_setzero_si256();
    v[14] = _mm256_set1_epi32(52);
    v[15] = _mm256_set1_epi32(0x0A); // flags

    __m256i m[16];
    for (int i = 0; i < 16; ++i) {
        if (i == 3) {
            m[i] = _mm256_set_epi32(
                base_nonce + 7, base_nonce + 6, base_nonce + 5, base_nonce + 4,
                base_nonce + 3, base_nonce + 2, base_nonce + 1, base_nonce + 0
            );
        } else {
            m[i] = _mm256_set1_epi32(block2_words[i]);
        }
    }

#define AVX2_ROUND_I(rnd, s0,s1,s2,s3,s4,s5,s6,s7,s8,s9,s10,s11,s12,s13,s14,s15) \
    avx2_g_func<(s0>=13), (s1>=13)>(v[0], v[4], v[8], v[12], m[s0], m[s1]); \
    avx2_g_func<(s2>=13), (s3>=13)>(v[1], v[5], v[9], v[13], m[s2], m[s3]); \
    avx2_g_func<(s4>=13), (s5>=13)>(v[2], v[6], v[10], v[14], m[s4], m[s5]); \
    avx2_g_func<(s6>=13), (s7>=13)>(v[3], v[7], v[11], v[15], m[s6], m[s7]); \
    avx2_g_func<(s8>=13), (s9>=13)>(v[0], v[5], v[10], v[15], m[s8], m[s9]); \
    avx2_g_func<(s10>=13), (s11>=13)>(v[1], v[6], v[11], v[12], m[s10], m[s11]); \
    avx2_g_func<(s12>=13), (s13>=13)>(v[2], v[7], v[8], v[13], m[s12], m[s13]); \
    avx2_g_func<(s14>=13), (s15>=13)>(v[3], v[4], v[9], v[14], m[s14], m[s15]);

    AVX2_ROUND_I(0, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15)
    AVX2_ROUND_I(1, 2,6,3,10,7,0,4,13,1,11,12,5,9,14,15,8)
    AVX2_ROUND_I(2, 3,4,10,12,13,2,7,14,6,5,9,0,11,15,8,1)
    AVX2_ROUND_I(3, 10,7,12,9,14,3,13,15,4,0,11,2,5,8,1,6)
    AVX2_ROUND_I(4, 12,13,9,11,15,10,14,8,7,2,5,3,0,1,6,4)
    AVX2_ROUND_I(5, 9,14,11,5,8,12,15,1,13,3,0,10,2,6,4,7)
    AVX2_ROUND_I(6, 11,15,5,0,1,9,8,6,14,10,2,12,3,4,7,13)

#undef AVX2_ROUND_I

    for (int i = 0; i < 8; ++i) {
        __m256i h = _mm256_xor_si256(v[i], v[i + 8]);
        _mm256_storeu_si256((__m256i*)out_hashes[i], h);
    }
}

inline std::optional<Solution> avx2_mine_batch(const uint32_t midstate[8], const uint32_t block2_words[16], uint32_t base_nonce, const uint32_t target[8]) {
    alignas(32) uint32_t hashes[8][8]; // [hash_word][lane]
    avx2_hash_batch(midstate, block2_words, base_nonce, hashes);

    for (int lane = 0; lane < 8; ++lane) {
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

inline std::optional<Solution> avx2_mine_range_impl(const uint32_t midstate[8], const uint32_t block2_words[16], uint32_t start_nonce, uint32_t count, const uint32_t target[8]) {
    for (uint32_t i = 0; i < count; i += 8) {
        auto sol = avx2_mine_batch(midstate, block2_words, start_nonce + i, target);
        if (sol) return sol;
    }
    return std::nullopt;
}

} // namespace blake3
} // namespace dcr

#endif // __AVX2__
