// DCR BLAKE3 Miner — BLAKE3 Constants
// All compile-time constants for the BLAKE3 hash function and
// Decred-specific parameters derived from the 180-byte header layout.
#pragma once

#include <cstdint>

namespace dcr::blake3 {

// ============================================================================
// BLAKE3 Initialization Vector
// Same as SHA-256: fractional parts of square roots of the first 8 primes.
// ============================================================================

inline constexpr uint32_t IV[8] = {
    0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
    0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u
};

// ============================================================================
// BLAKE3 Domain Separation Flags
// ============================================================================

inline constexpr uint32_t FLAG_CHUNK_START         = 1u << 0;  // 0x01
inline constexpr uint32_t FLAG_CHUNK_END           = 1u << 1;  // 0x02
inline constexpr uint32_t FLAG_PARENT              = 1u << 2;  // 0x04
inline constexpr uint32_t FLAG_ROOT                = 1u << 3;  // 0x08
inline constexpr uint32_t FLAG_KEYED_HASH          = 1u << 4;  // 0x10
inline constexpr uint32_t FLAG_DERIVE_KEY_CONTEXT  = 1u << 5;  // 0x20
inline constexpr uint32_t FLAG_DERIVE_KEY_MATERIAL = 1u << 6;  // 0x40

// ============================================================================
// BLAKE3 Structural Constants
// ============================================================================

inline constexpr size_t BLOCK_LEN  = 64;    // Bytes per compression block
inline constexpr size_t CHUNK_LEN  = 1024;  // Bytes per chunk (16 blocks)
inline constexpr size_t OUT_LEN    = 32;    // Default output length (256 bits)
inline constexpr int    NUM_ROUNDS = 7;     // Compression rounds

// ============================================================================
// BLAKE3 Rotation Constants (used in G quarter-round function)
// ============================================================================

inline constexpr int R1 = 16;  // First right rotation
inline constexpr int R2 = 12;  // Second right rotation
inline constexpr int R3 = 8;   // Third right rotation
inline constexpr int R4 = 7;   // Fourth right rotation

// ============================================================================
// Message Word Permutation Schedule
// MSG_SCHEDULE[round][position] → index into original 16 message words.
// Derived from base permutation σ = {2,6,3,10,7,0,4,13,1,11,12,5,9,14,15,8}
// applied iteratively for each round.
// ============================================================================

inline constexpr uint8_t MSG_SCHEDULE[7][16] = {
    //  0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 }, // Round 0
    {  2,  6,  3, 10,  7,  0,  4, 13,  1, 11, 12,  5,  9, 14, 15,  8 }, // Round 1
    {  3,  4, 10, 12, 13,  2,  7, 14,  6,  5,  9,  0, 11, 15,  8,  1 }, // Round 2
    { 10,  7, 12,  9, 14,  3, 13, 15,  4,  0, 11,  2,  5,  8,  1,  6 }, // Round 3
    { 12, 13,  9, 11, 15, 10, 14,  8,  7,  2,  5,  3,  0,  1,  6,  4 }, // Round 4
    {  9, 14, 11,  5,  8, 12, 15,  1, 13,  3,  0, 10,  2,  6,  4,  7 }, // Round 5
    { 11, 15,  5,  0,  1,  9,  8,  6, 14, 10,  2, 12,  3,  4,  7, 13 }, // Round 6
};

// ============================================================================
// Decred-Specific BLAKE3 Parameters
// For a 180-byte header processed as a single chunk with 3 blocks.
// ============================================================================

// --- Block flags for each of the 3 compression blocks ---
inline constexpr uint32_t DCR_BLOCK0_FLAGS = FLAG_CHUNK_START;             // 0x01
inline constexpr uint32_t DCR_BLOCK1_FLAGS = 0;                            // 0x00
inline constexpr uint32_t DCR_BLOCK2_FLAGS = FLAG_CHUNK_END | FLAG_ROOT;   // 0x0A

// --- Block 2 (final block) compression parameters (all constant) ---
inline constexpr uint32_t DCR_BLOCK2_LEN     = 52;  // 52 data bytes in final block
inline constexpr uint32_t DCR_BLOCK2_COUNTER_LO = 0; // Single chunk → counter = 0
inline constexpr uint32_t DCR_BLOCK2_COUNTER_HI = 0;

// Initial state rows 2-3 for block 2 (constant across ALL jobs and nonces):
//   v[8..11]  = IV[0..3]
//   v[12]     = counter_lo = 0
//   v[13]     = counter_hi = 0
//   v[14]     = block_len  = 52
//   v[15]     = flags      = 0x0A
inline constexpr uint32_t DCR_BLOCK2_STATE_ROW2[4] = {
    IV[0], IV[1], IV[2], IV[3]   // 0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A
};
inline constexpr uint32_t DCR_BLOCK2_STATE_ROW3[4] = {
    0, 0, 52, 0x0000000Au
};

// ============================================================================
// Decred Header Layout Constants
// ============================================================================

inline constexpr size_t DCR_HEADER_SIZE       = 180;
inline constexpr size_t DCR_NONCE_OFFSET      = 140;  // Nonce uint32 LE
inline constexpr size_t DCR_EXTRADATA_OFFSET  = 144;  // ExtraData [32]byte
inline constexpr size_t DCR_STAKEVERSION_OFFSET = 176; // StakeVersion uint32 LE

// BLAKE3 block boundaries within the 180-byte header
inline constexpr size_t DCR_BLOCK0_OFFSET = 0;    // Bytes 0-63
inline constexpr size_t DCR_BLOCK1_OFFSET = 64;   // Bytes 64-127
inline constexpr size_t DCR_BLOCK2_OFFSET = 128;  // Bytes 128-179

// --- Final block (block 2) message word layout ---
// Block 2 covers header bytes 128-179 (52 bytes) + 12 bytes zero padding.
// Each word is 4 bytes, little-endian decoded from the header.
//
// Word  Header Offset  Field           Changes per nonce?
// ----  -------------  --------------- ------------------
//  0    128-131        Height          Constant per job
//  1    132-135        Size            Constant per job
//  2    136-139        Timestamp       Constant per job (may update rarely)
//  3    140-143        Nonce           *** CHANGES EVERY HASH ***
//  4    144-147        ExtraData[0:4]  Constant per job (pool extranonce)
//  5    148-151        ExtraData[4:8]  Constant per job
//  6    152-155        ExtraData[8:12] Constant per job
//  7    156-159        ExtraData[12:16] Constant per job
//  8    160-163        ExtraData[16:20] Constant per job
//  9    164-167        ExtraData[20:24] Constant per job
// 10    168-171        ExtraData[24:28] Constant per job
// 11    172-175        ExtraData[28:32] Constant per job
// 12    176-179        StakeVersion    Constant per job
// 13    (padding)      0x00000000      Always zero
// 14    (padding)      0x00000000      Always zero
// 15    (padding)      0x00000000      Always zero

inline constexpr int DCR_NONCE_WORD_INDEX  = 3;   // m[3] = nonce
inline constexpr int DCR_BLOCK2_DATA_WORDS = 13;  // 52 bytes = 13 words
inline constexpr int DCR_BLOCK2_ZERO_START = 13;  // Words 13-15 always zero

// DCP-0011 recommended 64-bit nonce layout within ExtraData[36]:
// Bytes 140-147 (8 bytes): Miner nonce (uint64 LE) → words 3-4
// Bytes 148-151 (4 bytes): Pool nonce → word 5
// Bytes 152-175 (24 bytes): Zero-padded → words 6-11
// This allows a 2^64 nonce space before needing job refresh.
inline constexpr int DCR_NONCE64_WORD_LO = 3;  // Lower 32 bits of 64-bit nonce
inline constexpr int DCR_NONCE64_WORD_HI = 4;  // Upper 32 bits of 64-bit nonce

} // namespace dcr::blake3
