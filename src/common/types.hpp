// DCR BLAKE3 Miner — Shared Types and Utility Functions
// All types used across the miner codebase.
#pragma once

#include <cstdint>
#include <cstring>
#include <array>
#include <atomic>
#include <string>

namespace dcr {

// ============================================================================
// Core types
// ============================================================================

/// 256-bit hash (32 bytes). Byte order matches BLAKE3 output (little-endian
/// when interpreted as a 256-bit integer: byte[0]=LSB, byte[31]=MSB).
using Hash256 = std::array<uint8_t, 32>;

/// BLAKE3 chaining value / midstate: 8 × 32-bit words.
using ChainValue = std::array<uint32_t, 8>;

/// BLAKE3 message block: 16 × 32-bit words (64 bytes decoded as LE u32).
using MsgBlock = std::array<uint32_t, 16>;

/// 180-byte serialized Decred block header.
static constexpr size_t HEADER_SIZE = 180;
using HeaderBytes = std::array<uint8_t, HEADER_SIZE>;

/// Difficulty target as 8 × 32-bit words (word[0]=LSW, word[7]=MSW).
using Target256 = std::array<uint32_t, 8>;

// ============================================================================
// Mining result
// ============================================================================

struct Solution {
    uint64_t    nonce = 0;          // Full 64-bit miner nonce (or 32-bit nonce)
    uint32_t    extra_nonce2 = 0;   // Pool extranonce2 if applicable
    std::string extra_nonce2_hex;   // Hex string of extranonce2 for stratum submit
    Hash256     pow_hash{};         // The PoW hash that met the target (32 bytes)
    uint32_t    hash_words[8]{};    // 256-bit hash as 8 x 32-bit words
    uint32_t    device_id = 0;      // Which device found this
};

// ============================================================================
// Backend identification
// ============================================================================

enum class BackendType : uint8_t {
    CPU_SCALAR = 0,
    CPU_SSE41,
    CPU_AVX2,
    CPU_AVX512,
    CUDA,
    HIP,
    OPENCL,
};

inline const char* backend_name(BackendType t) {
    switch (t) {
        case BackendType::CPU_SCALAR: return "Scalar";
        case BackendType::CPU_SSE41:  return "SSE4.1";
        case BackendType::CPU_AVX2:   return "AVX2";
        case BackendType::CPU_AVX512: return "AVX-512";
        case BackendType::CUDA:       return "CUDA";
        case BackendType::HIP:        return "HIP";
        case BackendType::OPENCL:     return "OpenCL";
    }
    return "Unknown";
}

// ============================================================================
// Little-endian read/write helpers (portable, no alignment requirement)
// ============================================================================

inline uint16_t read_le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0])
         | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t read_le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t read_le64(const uint8_t* p) {
    return static_cast<uint64_t>(read_le32(p))
         | (static_cast<uint64_t>(read_le32(p + 4)) << 32);
}

inline void write_le16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

inline void write_le32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

inline void write_le64(uint8_t* p, uint64_t v) {
    write_le32(p, static_cast<uint32_t>(v));
    write_le32(p + 4, static_cast<uint32_t>(v >> 32));
}

// ============================================================================
// Bit manipulation
// ============================================================================

/// 32-bit right rotate (compile-time evaluable).
inline constexpr uint32_t rotr32(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

// ============================================================================
// Hex conversion utilities
// ============================================================================

/// Convert a hex character to its 4-bit value. Returns -1 on invalid input.
inline int hex_char_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/// Decode hex string to byte array. Returns false on invalid input.
inline bool hex_decode(const char* hex, uint8_t* out, size_t byte_count) {
    for (size_t i = 0; i < byte_count; i++) {
        int hi = hex_char_val(hex[i * 2]);
        int lo = hex_char_val(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

/// Encode byte array to hex string.
inline std::string hex_encode(const uint8_t* data, size_t len) {
    static const char hex_chars[] = "0123456789abcdef";
    std::string result(len * 2, '0');
    for (size_t i = 0; i < len; i++) {
        result[i * 2]     = hex_chars[(data[i] >> 4) & 0xF];
        result[i * 2 + 1] = hex_chars[data[i] & 0xF];
    }
    return result;
}

/// Reverse byte order of a 32-byte hash (for display: internal LE ↔ display BE).
inline Hash256 reverse_hash(const Hash256& h) {
    Hash256 r;
    for (int i = 0; i < 32; i++) {
        r[i] = h[31 - i];
    }
    return r;
}

} // namespace dcr
