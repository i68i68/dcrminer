#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include "../common/types.hpp"

namespace dcr {
namespace blake3 {

// Dynamic CPU capability checks
bool has_avx2_runtime();
bool has_avx512_runtime();

// Dispatched mining functions across architectures
std::optional<Solution> avx2_mine_range(const uint32_t midstate[8], const uint32_t block2_words[16], uint32_t start_nonce, uint32_t count, const uint32_t target[8]);
std::optional<Solution> avx512_mine_range(const uint32_t midstate[8], const uint32_t block2_words[16], uint32_t start_nonce, uint32_t count, const uint32_t target[8]);

// Direct batch hashing for equivalence verification
void avx2_hash_batch_dispatch(const uint32_t midstate[8], const uint32_t block2_words[16], uint32_t base_nonce, uint32_t out_hashes[8][8]);
void avx512_hash_batch_dispatch(const uint32_t midstate[8], const uint32_t block2_words[16], uint32_t base_nonce, uint32_t out_hashes[8][16]);

// Hardware CPU brand string detection
std::string get_cpu_brand_string();

} // namespace blake3
} // namespace dcr
