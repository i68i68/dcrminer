#pragma once

#include "../common/types.hpp"
#include "../core/target.hpp"
#include "../blake3/blake3_ref.hpp"
#include "../decred/header.hpp"
#include <atomic>

namespace dcr {

struct MiningJob {
    uint64_t job_id;
    ChainValue midstate;        // Precomputed after blocks 0-1
    MsgBlock block2_words;      // Template for block 2 (nonce at word 3)
    Target256 share_target;     // Pool share target
    Target256 network_target;   // Network block target
    uint32_t height;
    uint32_t bits;
    double difficulty;
    HeaderBytes header_template; // Full 180-byte template
    std::atomic<bool> stale{false};
    bool is_devfee{false};

    MiningJob() = default;
    
    MiningJob(const MiningJob& other) {
        job_id = other.job_id;
        midstate = other.midstate;
        block2_words = other.block2_words;
        share_target = other.share_target;
        network_target = other.network_target;
        height = other.height;
        bits = other.bits;
        difficulty = other.difficulty;
        header_template = other.header_template;
        stale.store(other.stale.load());
        is_devfee = other.is_devfee;
    }

    MiningJob& operator=(const MiningJob& other) {
        job_id = other.job_id;
        midstate = other.midstate;
        block2_words = other.block2_words;
        share_target = other.share_target;
        network_target = other.network_target;
        height = other.height;
        bits = other.bits;
        difficulty = other.difficulty;
        header_template = other.header_template;
        stale.store(other.stale.load());
        is_devfee = other.is_devfee;
        return *this;
    }

    static MiningJob from_header(const HeaderBytes& header, uint32_t bits) {
        Target256 target = compact_to_target(bits);
        return from_header(header, target, target);
    }

    static MiningJob from_header(const HeaderBytes& header, const Target256& share_target, const Target256& net_target) {
        MiningJob job;
        job.job_id = 0; // Set by caller usually
        job.header_template = header;
        job.bits = DcrHeader::get_bits(header);
        job.height = DcrHeader::get_height(header);
        job.share_target = share_target;
        job.network_target = net_target;
        job.difficulty = difficulty_from_bits(job.bits);
        job.midstate = blake3::compute_midstate(header);
        job.block2_words = blake3::prepare_block2_words(header);
        job.stale.store(false);
        return job;
    }
};

} // namespace dcr
