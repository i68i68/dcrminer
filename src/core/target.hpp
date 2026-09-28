// DCR BLAKE3 Miner — Target Comparison
// Implements compact target decoding (CompactToBig) and 256-bit comparison
// for Decred Proof-of-Work validation.
//
// Hash and target are both unsigned 256-bit integers in little-endian word order:
//   word[0] = least significant 32 bits
//   word[7] = most significant 32 bits
//
// A valid PoW requires: hash_256 <= target_256
#pragma once

#include "../common/types.hpp"
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace dcr {

// ============================================================================
// Compact Target Decoding
// Decred uses the same compact format as Bitcoin:
//   bits = (exponent << 24) | mantissa
//   exponent: top 8 bits (byte length of full target)
//   mantissa: lower 24 bits (with sign bit at bit 23)
//   target = mantissa × 256^(exponent - 3)
// ============================================================================

/// Decode compact "Bits" field to a 256-bit target.
/// Returns all-zero target for invalid/negative values.
inline Target256 compact_to_target(uint32_t bits) {
    Target256 target{};

    uint32_t mantissa = bits & 0x007FFFFFu;
    uint32_t exponent = bits >> 24;
    bool negative = (bits & 0x00800000u) != 0;

    // Reject negative or zero mantissa
    if (negative || mantissa == 0) {
        return target;
    }

    if (exponent <= 3) {
        // Shift mantissa right to fit in fewer bytes
        mantissa >>= 8 * (3 - exponent);
        target[0] = mantissa;
    } else {
        // Place 3-byte mantissa at byte offset (exponent - 3)
        uint32_t byte_off = exponent - 3;
        uint32_t word_idx = byte_off / 4;
        uint32_t bit_shift = (byte_off % 4) * 8;

        if (word_idx < 8) {
            uint64_t shifted = static_cast<uint64_t>(mantissa) << bit_shift;
            target[word_idx] = static_cast<uint32_t>(shifted);
            if (word_idx + 1 < 8) {
                target[word_idx + 1] = static_cast<uint32_t>(shifted >> 32);
            }
        }
    }

    return target;
}

// ============================================================================
// 256-bit Target Comparison
// ============================================================================

/// Full 256-bit comparison: hash <= target.
/// Compares from MSW (word 7) to LSW (word 0) for early exit.
/// Returns true if the hash meets the difficulty target.
inline bool meets_target(const uint32_t hash_words[8], const Target256& target) {
    for (int i = 7; i >= 0; i--) {
        if (hash_words[i] < target[i]) return true;   // hash < target → valid
        if (hash_words[i] > target[i]) return false;   // hash > target → invalid
    }
    return true;  // hash == target → valid
}

/// Overload accepting Hash256 (byte array → word conversion).
inline bool meets_target(const Hash256& hash, const Target256& target) {
    uint32_t words[8];
    for (int i = 0; i < 8; i++) {
        words[i] = read_le32(hash.data() + i * 4);
    }
    return meets_target(words, target);
}

/// Quick MSW pre-check for GPU/CPU inner loop.
/// Returns false if hash definitely exceeds target (based on MSW only).
/// Returns true if full comparison is needed or hash might be valid.
/// Use this to eliminate >99.99% of candidates at high difficulty.
inline bool quick_target_check(uint32_t hash_msw, uint32_t target_msw) {
    return hash_msw <= target_msw;
}

/// Two-word quick check for even higher difficulty.
inline bool quick_target_check2(uint32_t hash_w7, uint32_t hash_w6,
                                 uint32_t target_w7, uint32_t target_w6) {
    if (hash_w7 != target_w7) return hash_w7 < target_w7;
    return hash_w6 <= target_w6;
}

// ============================================================================
// Difficulty Calculation
// ============================================================================

/// Compute approximate difficulty from compact bits.
/// difficulty = pow_limit / target
/// For display purposes only.
inline double difficulty_from_bits(uint32_t bits) {
    // Mainnet PoW limit = 2^224 - 1 ≈ 0xFFFF × 2^208
    // With compact bits 0x1E00FFFF

    uint32_t mantissa = bits & 0x007FFFFFu;
    uint32_t exponent = bits >> 24;

    if (mantissa == 0) return 0.0;

    // difficulty = (0xFFFF × 2^208) / (mantissa × 2^(8*(exponent-3)))
    //            = (0xFFFF / mantissa) × 2^(208 - 8*(exponent-3))
    double diff = static_cast<double>(0xFFFF) / static_cast<double>(mantissa);

    int shift = 208 - 8 * (static_cast<int>(exponent) - 3);
    if (shift > 0) {
        diff *= static_cast<double>(1ULL << std::min(shift, 63));
        shift -= std::min(shift, 63);
        while (shift > 0) {
            diff *= static_cast<double>(1ULL << std::min(shift, 63));
            shift -= std::min(shift, 63);
        }
    } else if (shift < 0) {
        diff /= static_cast<double>(1ULL << std::min(-shift, 63));
    }

    return diff;
}

/// Convert Stratum / Pool difficulty to a 256-bit target.
/// target = diff1_target / difficulty
/// Stratum difficulty 1 target is 0x00000000FFFF0000... (0x1d00ffff)
inline Target256 difficulty_to_target(double diff) {
    Target256 target{};
    if (diff <= 0.0) {
        return compact_to_target(0x1d00ffff);
    }

    // Stratum diff 1 target has 0xFFFF0000 in word 6, corresponding to 0xFFFF * 2^208.
    // target = (0xFFFF0000 * 2^192) / diff
    double rem = (static_cast<double>(0xFFFF0000ULL)) / diff;

    // Check for overflow (diff extremely small, e.g. < 2^-32)
    if (rem >= 18446744073709551616.0) { // >= 2^64
        for (int i = 0; i < 8; i++) target[i] = 0xFFFFFFFFu;
        return target;
    }

    // Word 7
    double w7 = rem / 4294967296.0;
    if (w7 >= 4294967296.0) {
        for (int i = 0; i < 8; i++) target[i] = 0xFFFFFFFFu;
        return target;
    }
    uint32_t u7 = static_cast<uint32_t>(w7);
    target[7] = u7;
    rem -= static_cast<double>(u7) * 4294967296.0;

    // Words 6 down to 0
    for (int i = 6; i >= 0; i--) {
        uint32_t ui = static_cast<uint32_t>(std::clamp(rem, 0.0, 4294967295.0));
        target[i] = ui;
        rem = (rem - static_cast<double>(ui)) * 4294967296.0;
    }

    return target;
}

} // namespace dcr
