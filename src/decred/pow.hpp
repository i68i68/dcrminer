#pragma once

#include "../common/types.hpp"
#include "../blake3/blake3_ref.hpp"
#include "../core/target.hpp"

namespace dcr {

/// Compute BLAKE3 PoW hash of 180-byte header
inline Hash256 pow_hash(const HeaderBytes& header_bytes) {
    return blake3::blake3_hash_header(header_bytes);
}

/// Check if header meets target
inline bool validate_pow(const HeaderBytes& header_bytes, const Target256& target) {
    Hash256 hash = pow_hash(header_bytes);
    return meets_target(hash, target);
}

/// Output as words for comparison
inline void pow_hash_words(const HeaderBytes& header_bytes, uint32_t out_words[8]) {
    Hash256 hash = pow_hash(header_bytes);
    for (int i = 0; i < 8; i++) {
        out_words[i] = read_le32(hash.data() + i * 4);
    }
}

} // namespace dcr
