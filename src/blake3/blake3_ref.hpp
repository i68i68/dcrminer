// DCR BLAKE3 Miner — Reference BLAKE3 Implementation
// Complete, correct, unoptimized BLAKE3 for use as the correctness oracle.
// Handles single-chunk inputs (≤1024 bytes), which covers DCR's 180 bytes.
//
// This implementation is NOT used in the mining hot path.
// Its sole purpose is to verify that optimized backends produce identical output.
#pragma once

#include "blake3_constants.hpp"
#include "../common/types.hpp"
#include <cstring>
#include <algorithm>

namespace dcr::blake3 {

// ============================================================================
// BLAKE3 Compression Function
// Processes one 64-byte message block with an 8-word chaining value.
// Outputs full 16-word state.
// ============================================================================

inline void compress_ref(
    const uint32_t cv[8],        // 8-word input chaining value
    const uint32_t block[16],    // 16 message words (LE decoded)
    uint64_t counter,            // Chunk counter
    uint32_t block_len,          // Actual data bytes in block (0-64)
    uint32_t flags,              // Domain separation flags
    uint32_t out[16]             // 16-word output state
) {
    // Initialize 4×4 state matrix:
    //   Row 0-1: chaining value
    //   Row 2:   IV[0..3]
    //   Row 3:   counter_lo, counter_hi, block_len, flags
    uint32_t v[16] = {
        cv[0], cv[1], cv[2], cv[3],
        cv[4], cv[5], cv[6], cv[7],
        IV[0], IV[1], IV[2], IV[3],
        static_cast<uint32_t>(counter),         // counter low
        static_cast<uint32_t>(counter >> 32),    // counter high
        block_len,
        flags
    };

    // 7 rounds of mixing with message word permutation
    for (int round = 0; round < NUM_ROUNDS; round++) {
        const uint8_t* s = MSG_SCHEDULE[round];

        // Column step: G on columns (0,4,8,12), (1,5,9,13), (2,6,10,14), (3,7,11,15)
        #define G_REF(a, b, c, d, mx, my) do { \
            v[a] = v[a] + v[b] + (mx); \
            v[d] = rotr32(v[d] ^ v[a], R1); \
            v[c] = v[c] + v[d]; \
            v[b] = rotr32(v[b] ^ v[c], R2); \
            v[a] = v[a] + v[b] + (my); \
            v[d] = rotr32(v[d] ^ v[a], R3); \
            v[c] = v[c] + v[d]; \
            v[b] = rotr32(v[b] ^ v[c], R4); \
        } while(0)

        // Columns
        G_REF( 0,  4,  8, 12, block[s[ 0]], block[s[ 1]]);
        G_REF( 1,  5,  9, 13, block[s[ 2]], block[s[ 3]]);
        G_REF( 2,  6, 10, 14, block[s[ 4]], block[s[ 5]]);
        G_REF( 3,  7, 11, 15, block[s[ 6]], block[s[ 7]]);

        // Diagonals
        G_REF( 0,  5, 10, 15, block[s[ 8]], block[s[ 9]]);
        G_REF( 1,  6, 11, 12, block[s[10]], block[s[11]]);
        G_REF( 2,  7,  8, 13, block[s[12]], block[s[13]]);
        G_REF( 3,  4,  9, 14, block[s[14]], block[s[15]]);

        #undef G_REF
    }

    // Finalize: XOR upper and lower halves
    for (int i = 0; i < 8; i++) {
        out[i] = v[i] ^ v[i + 8];
    }
    // Second half of output (needed for XOF mode, not for 256-bit mining hash)
    for (int i = 8; i < 16; i++) {
        out[i] = v[i] ^ cv[i - 8];
    }
}

// ============================================================================
// BLAKE3 Hash (single-chunk, ≤1024 bytes)
// Standard unkeyed BLAKE3-256 hash.
// ============================================================================

inline Hash256 blake3_hash(const uint8_t* input, size_t len) {
    // Verify single-chunk constraint
    if (len > CHUNK_LEN) {
        // Multi-chunk not implemented (not needed for DCR mining).
        // A production reference would handle the Merkle tree here.
        Hash256 zero{};
        return zero;
    }

    // Initial chaining value = IV
    uint32_t cv[8];
    std::memcpy(cv, IV, sizeof(cv));

    // Calculate number of blocks
    size_t num_blocks = (len == 0) ? 1 : (len + BLOCK_LEN - 1) / BLOCK_LEN;

    size_t pos = 0;
    for (size_t b = 0; b < num_blocks; b++) {
        // Determine how many data bytes are in this block
        size_t remaining = (len > pos) ? (len - pos) : 0;
        uint32_t blen = static_cast<uint32_t>(std::min(remaining, BLOCK_LEN));

        // Decode message words from input bytes (with zero padding)
        uint8_t padded[64] = {};
        if (blen > 0) {
            std::memcpy(padded, input + pos, blen);
        }
        uint32_t msg[16];
        for (int i = 0; i < 16; i++) {
            msg[i] = read_le32(padded + i * 4);
        }

        // Determine flags for this block
        uint32_t flags = 0;
        if (b == 0) {
            flags |= FLAG_CHUNK_START;
        }
        if (b == num_blocks - 1) {
            flags |= FLAG_CHUNK_END | FLAG_ROOT;
        }

        // Compress
        uint32_t out[16];
        compress_ref(cv, msg, /*counter=*/0, blen, flags, out);

        if (b < num_blocks - 1) {
            // Intermediate block: extract chaining value (first 8 words)
            std::memcpy(cv, out, 8 * sizeof(uint32_t));
        } else {
            // Final block (ROOT): extract 256-bit hash from first 8 words
            Hash256 hash;
            for (int i = 0; i < 8; i++) {
                write_le32(hash.data() + i * 4, out[i]);
            }
            return hash;
        }

        pos += BLOCK_LEN;
    }

    // Should never reach here
    return Hash256{};
}

// ============================================================================
// Convenience: hash a 180-byte Decred block header
// ============================================================================

inline Hash256 blake3_hash_header(const HeaderBytes& header) {
    return blake3_hash(header.data(), HEADER_SIZE);
}

// ============================================================================
// Decomposed operations for midstate verification
// ============================================================================

/// Process blocks 0 and 1 of a 180-byte header to produce the midstate.
/// This is the chaining value after compressing bytes 0-127.
/// Midstate is constant for all nonce values within a job.
inline ChainValue compute_midstate(const HeaderBytes& header) {
    uint32_t cv[8];
    std::memcpy(cv, IV, sizeof(cv));

    // Block 0: bytes 0-63, flags = CHUNK_START
    {
        uint32_t msg[16];
        for (int i = 0; i < 16; i++) {
            msg[i] = read_le32(header.data() + i * 4);
        }
        uint32_t out[16];
        compress_ref(cv, msg, 0, 64, DCR_BLOCK0_FLAGS, out);
        std::memcpy(cv, out, sizeof(cv));
    }

    // Block 1: bytes 64-127, flags = 0
    {
        uint32_t msg[16];
        for (int i = 0; i < 16; i++) {
            msg[i] = read_le32(header.data() + 64 + i * 4);
        }
        uint32_t out[16];
        compress_ref(cv, msg, 0, 64, DCR_BLOCK1_FLAGS, out);
        std::memcpy(cv, out, sizeof(cv));
    }

    ChainValue result;
    std::memcpy(result.data(), cv, sizeof(cv));
    return result;
}

/// Process block 2 (final block) with a given midstate and nonce.
/// Returns the 256-bit PoW hash.
inline Hash256 hash_final_block_ref(
    const ChainValue& midstate,
    const MsgBlock& block2_words,
    uint32_t nonce
) {
    // Copy message words and insert nonce at position 3
    uint32_t msg[16];
    std::memcpy(msg, block2_words.data(), sizeof(msg));
    msg[DCR_NONCE_WORD_INDEX] = nonce;

    // Compress with midstate as chaining value
    uint32_t out[16];
    compress_ref(midstate.data(), msg, 0, DCR_BLOCK2_LEN, DCR_BLOCK2_FLAGS, out);

    // Extract 256-bit hash
    Hash256 hash;
    for (int i = 0; i < 8; i++) {
        write_le32(hash.data() + i * 4, out[i]);
    }
    return hash;
}

/// Prepare block 2 message words from header bytes 128-179.
/// Zero-pads words 13-15.
inline MsgBlock prepare_block2_words(const HeaderBytes& header) {
    MsgBlock block2{};  // Zero-initialized (handles padding)
    for (int i = 0; i < DCR_BLOCK2_DATA_WORDS; i++) {
        block2[i] = read_le32(header.data() + DCR_BLOCK2_OFFSET + i * 4);
    }
    return block2;
}

} // namespace dcr::blake3
