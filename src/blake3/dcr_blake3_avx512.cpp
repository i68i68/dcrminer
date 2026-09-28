#include "dcr_blake3_dispatch.hpp"
#include "dcr_blake3_avx512.hpp"
#include "dcr_blake3_scalar.hpp"

namespace dcr {
namespace blake3 {

bool has_avx512_runtime() {
#if defined(__AVX512F__)
    return true;
#else
    return false;
#endif
}

std::optional<Solution> avx512_mine_range(const uint32_t midstate[8], const uint32_t block2_words[16], uint32_t start_nonce, uint32_t count, const uint32_t target[8]) {
#if defined(__AVX512F__)
    return avx512_mine_range_impl(midstate, block2_words, start_nonce, count, target);
#else
    return scalar_mine_range(midstate, block2_words, start_nonce, count, target);
#endif
}

void avx512_hash_batch_dispatch(const uint32_t midstate[8], const uint32_t block2_words[16], uint32_t base_nonce, uint32_t out_hashes[8][16]) {
#if defined(__AVX512F__)
    avx512_hash_batch(midstate, block2_words, base_nonce, out_hashes);
#else
    for (int lane = 0; lane < 16; ++lane) {
        uint32_t h[8];
        scalar_hash_final_block(midstate, block2_words, base_nonce + lane, h);
        for (int w = 0; w < 8; ++w) {
            out_hashes[w][lane] = h[w];
        }
    }
#endif
}

} // namespace blake3
} // namespace dcr
