// DCR BLAKE3 Miner — Decred Block Header
// Exact 180-byte header layout per dcrd wire protocol and DCP-0011.
#pragma once

#include "../common/types.hpp"
#include <cstring>
#include <string>

namespace dcr {

// ============================================================================
// Header Field Offsets (verified against dcrd/wire/blockheader.go)
// ============================================================================

struct HeaderOffsets {
    static constexpr size_t VERSION        = 0;    // int32 LE, 4 bytes
    static constexpr size_t PREV_BLOCK     = 4;    // [32]byte
    static constexpr size_t MERKLE_ROOT    = 36;   // [32]byte
    static constexpr size_t STAKE_ROOT     = 68;   // [32]byte (CommitmentRoot per DCP-0005)
    static constexpr size_t VOTE_BITS      = 100;  // uint16 LE
    static constexpr size_t FINAL_STATE    = 102;  // [6]byte
    static constexpr size_t VOTERS         = 108;  // uint16 LE
    static constexpr size_t FRESH_STAKE    = 110;  // uint8
    static constexpr size_t REVOCATIONS    = 111;  // uint8
    static constexpr size_t POOL_SIZE      = 112;  // uint32 LE
    static constexpr size_t BITS           = 116;  // uint32 LE (compact target)
    static constexpr size_t SBITS          = 120;  // int64 LE (stake difficulty)
    static constexpr size_t HEIGHT         = 128;  // uint32 LE
    static constexpr size_t SIZE           = 132;  // uint32 LE
    static constexpr size_t TIMESTAMP      = 136;  // uint32 LE (Unix seconds)
    static constexpr size_t NONCE          = 140;  // uint32 LE
    static constexpr size_t EXTRA_DATA     = 144;  // [32]byte
    static constexpr size_t STAKE_VERSION  = 176;  // uint32 LE
    static constexpr size_t TOTAL          = 180;

    // BLAKE3 block boundaries
    static constexpr size_t BLOCK0_START   = 0;
    static constexpr size_t BLOCK1_START   = 64;
    static constexpr size_t BLOCK2_START   = 128;
};

// ============================================================================
// Parsed Decred Block Header
// ============================================================================

struct DcrHeader {
    int32_t  version;
    Hash256  prev_block;       // Internal byte order (LE of hash output)
    Hash256  merkle_root;
    Hash256  stake_root;       // aka CommitmentRoot
    uint16_t vote_bits;
    uint8_t  final_state[6];
    uint16_t voters;
    uint8_t  fresh_stake;
    uint8_t  revocations;
    uint32_t pool_size;
    uint32_t bits;             // Compact difficulty target
    int64_t  sbits;            // Stake difficulty in atoms
    uint32_t height;
    uint32_t size;
    uint32_t timestamp;        // Unix seconds
    uint32_t nonce;
    uint8_t  extra_data[32];
    uint32_t stake_version;

    // ========================================================================
    // Serialization (to 180-byte wire format)
    // ========================================================================

    void serialize(HeaderBytes& out) const {
        size_t off = 0;
        auto put32 = [&](uint32_t v) { write_le32(out.data() + off, v); off += 4; };
        auto put16 = [&](uint16_t v) { write_le16(out.data() + off, v); off += 2; };
        auto put64 = [&](uint64_t v) { write_le64(out.data() + off, v); off += 8; };
        auto put8  = [&](uint8_t v)  { out[off++] = v; };
        auto putN  = [&](const void* src, size_t n) {
            std::memcpy(out.data() + off, src, n); off += n;
        };

        put32(static_cast<uint32_t>(version));
        putN(prev_block.data(), 32);
        putN(merkle_root.data(), 32);
        putN(stake_root.data(), 32);
        put16(vote_bits);
        putN(final_state, 6);
        put16(voters);
        put8(fresh_stake);
        put8(revocations);
        put32(pool_size);
        put32(bits);
        put64(static_cast<uint64_t>(sbits));
        put32(height);
        put32(size);
        put32(timestamp);
        put32(nonce);
        putN(extra_data, 32);
        put32(stake_version);
        // off == 180
    }

    // ========================================================================
    // Deserialization (from 180-byte wire format)
    // ========================================================================

    static DcrHeader deserialize(const HeaderBytes& data) {
        DcrHeader h{};
        const uint8_t* p = data.data();
        size_t off = 0;

        h.version = static_cast<int32_t>(read_le32(p + off)); off += 4;
        std::memcpy(h.prev_block.data(), p + off, 32); off += 32;
        std::memcpy(h.merkle_root.data(), p + off, 32); off += 32;
        std::memcpy(h.stake_root.data(), p + off, 32); off += 32;
        h.vote_bits = read_le16(p + off); off += 2;
        std::memcpy(h.final_state, p + off, 6); off += 6;
        h.voters = read_le16(p + off); off += 2;
        h.fresh_stake = p[off++];
        h.revocations = p[off++];
        h.pool_size = read_le32(p + off); off += 4;
        h.bits = read_le32(p + off); off += 4;
        h.sbits = static_cast<int64_t>(read_le64(p + off)); off += 8;
        h.height = read_le32(p + off); off += 4;
        h.size = read_le32(p + off); off += 4;
        h.timestamp = read_le32(p + off); off += 4;
        h.nonce = read_le32(p + off); off += 4;
        std::memcpy(h.extra_data, p + off, 32); off += 32;
        h.stake_version = read_le32(p + off); off += 4;

        return h;
    }

    // ========================================================================
    // In-place header mutation (for mining hot path — works on raw bytes)
    // ========================================================================

    /// Set 32-bit nonce in serialized header.
    static void set_nonce(HeaderBytes& data, uint32_t nonce) {
        write_le32(data.data() + HeaderOffsets::NONCE, nonce);
    }

    /// Set 64-bit miner nonce (DCP-0011 recommended layout).
    /// Occupies bytes 140-147: Nonce(4) + ExtraData[0:4](4).
    static void set_nonce64(HeaderBytes& data, uint64_t nonce) {
        write_le64(data.data() + HeaderOffsets::NONCE, nonce);
    }

    /// Set pool extranonce at ExtraData[4:8] (bytes 148-151).
    static void set_pool_nonce(HeaderBytes& data, uint32_t pool_nonce) {
        write_le32(data.data() + HeaderOffsets::EXTRA_DATA + 4, pool_nonce);
    }

    /// Set timestamp in serialized header.
    static void set_timestamp(HeaderBytes& data, uint32_t ts) {
        write_le32(data.data() + HeaderOffsets::TIMESTAMP, ts);
    }

    /// Read nonce from serialized header.
    static uint32_t get_nonce(const HeaderBytes& data) {
        return read_le32(data.data() + HeaderOffsets::NONCE);
    }

    /// Read bits (compact target) from serialized header.
    static uint32_t get_bits(const HeaderBytes& data) {
        return read_le32(data.data() + HeaderOffsets::BITS);
    }

    /// Read height from serialized header.
    static uint32_t get_height(const HeaderBytes& data) {
        return read_le32(data.data() + HeaderOffsets::HEIGHT);
    }

    /// Read timestamp from serialized header.
    static uint32_t get_timestamp(const HeaderBytes& data) {
        return read_le32(data.data() + HeaderOffsets::TIMESTAMP);
    }

    // ========================================================================
    // Stratum header construction
    // Assembles a 180-byte header from Stratum mining.notify parameters.
    // ========================================================================

    /// Build header from Stratum notify parameters.
    /// @param version      4-byte hex (params[5])
    /// @param prev_block   32-byte hex (params[1])
    /// @param mid_header   100-byte hex (params[2]) — covers offsets 36-135
    /// @param stake_ver    4-byte hex (params[3])
    /// @param ntime        4-byte hex (params[7])
    /// @param extra_nonce1 Pool-assigned extranonce1
    /// @param en1_size     Size of extranonce1 in bytes
    /// @param extra_nonce2 Miner extranonce2
    /// @param en2_size     Size of extranonce2 in bytes
    static bool build_from_stratum(
        HeaderBytes& out,
        const uint8_t* version_bytes,     // 4 bytes
        const uint8_t* prev_block_bytes,  // 32 bytes
        const uint8_t* mid_header_bytes,  // 100 bytes (offsets 36-135)
        const uint8_t* stake_ver_bytes,   // 4 bytes
        const uint8_t* ntime_bytes,       // 4 bytes
        const uint8_t* extra_nonce1, size_t en1_size,
        const uint8_t* extra_nonce2, size_t en2_size
    ) {
        // Zero-initialize
        out = {};

        // Version: offset 0
        std::memcpy(out.data() + 0, version_bytes, 4);
        // PrevBlock: offset 4
        std::memcpy(out.data() + 4, prev_block_bytes, 32);
        // Mid-header (MerkleRoot through Size): offset 36-135
        std::memcpy(out.data() + 36, mid_header_bytes, 100);
        // Timestamp: offset 136
        std::memcpy(out.data() + 136, ntime_bytes, 4);
        // Nonce: offset 140 (starts at 0, miner fills in)
        // ExtraData: offset 144
        if (en1_size > 0 && en1_size <= 32) {
            std::memcpy(out.data() + 144, extra_nonce1, en1_size);
        }
        if (en2_size > 0 && en1_size + en2_size <= 32) {
            std::memcpy(out.data() + 144 + en1_size, extra_nonce2, en2_size);
        }
        // StakeVersion: offset 176
        std::memcpy(out.data() + 176, stake_ver_bytes, 4);

        return true;
    }
};

} // namespace dcr
