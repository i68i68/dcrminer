#include "miner.hpp"
#include "../blake3/cpu_features.hpp"
#include "../blake3/dcr_blake3_dispatch.hpp"
#include "../blake3/dcr_blake3_scalar.hpp"

#if defined(DCR_ENABLE_CUDA)
#include "../gpu/cuda/cuda_backend.hpp"
#endif

#if defined(DCR_ENABLE_HIP)
#include "../gpu/hip/hip_backend.hpp"
#endif

#if defined(DCR_ENABLE_OPENCL)
#include "../gpu/opencl/opencl_backend.hpp"
#endif

#include "../telemetry/hardware_monitor.hpp"

#include <iostream>
#include <chrono>

namespace dcr {

Miner::Miner() : m_running(false), m_cpu_threads(0) {
    m_hw_monitor = std::make_unique<telemetry::HardwareMonitor>();
}

Miner::~Miner() {
    stop();
}

void Miner::set_cpu_threads(int threads) {
    if (threads < 0) {
        m_cpu_threads = static_cast<int>(std::thread::hardware_concurrency());
        if (m_cpu_threads <= 0) m_cpu_threads = 4;
    } else {
        m_cpu_threads = threads;
    }
}

void Miner::enable_cuda(const std::vector<int>& devices) {
    m_cuda_devices = devices;
}

void Miner::enable_hip(const std::vector<int>& devices) {
    m_hip_devices = devices;
}

void Miner::enable_opencl(const std::vector<int>& devices) {
    m_opencl_devices = devices;
}

void Miner::update_job(std::shared_ptr<MiningJob> new_job) {
    {
        std::lock_guard<std::mutex> lock(m_job_mutex);
        if (m_current_job) {
            m_current_job->stale.store(true, std::memory_order_relaxed);
        }
        m_current_job = new_job;
    }
    m_nonce_alloc.reset();
}

uint64_t Miner::get_total_hashes() const {
    uint64_t total = 0;
    for (size_t i = 0; i < m_stats.num_devices(); ++i) {
        auto* dev = const_cast<MinerStats&>(m_stats).get_device(i);
        if (dev) {
            total += dev->hashes.load(std::memory_order_relaxed);
        }
    }
    return total;
}

void Miner::start() {
    if (m_running.load()) return;
    m_running.store(true);

    m_stats.clear();

    size_t dev_idx = 0;
    if (m_cpu_threads > 0) {
        size_t cpu_stat_idx = dev_idx++;
        std::string cpu_brand = blake3::get_cpu_brand_string();
        m_stats.add_device("CPU [" + cpu_brand + " " + std::to_string(m_cpu_threads) + "T]", "CPU", 0);
        for (int i = 0; i < m_cpu_threads; ++i) {
            m_workers.emplace_back(&Miner::cpu_worker_thread, this, i);
        }
    }

    for (int dev : m_cuda_devices) {
        size_t gpu_idx = dev_idx++;
        std::string name = "GPU #" + std::to_string(dev) + " [CUDA]";
        if (m_hw_monitor && m_hw_monitor->is_nvml_available()) {
            auto tel = m_hw_monitor->get_gpu_telemetry(dev);
            if (!tel.name.empty()) name = tel.name;
        }
        m_stats.add_device(name, "GPU", dev);
        m_workers.emplace_back(&Miner::gpu_worker_thread, this, dev, BackendType::CUDA, gpu_idx);
    }

    for (int dev : m_hip_devices) {
        size_t gpu_idx = dev_idx++;
        m_stats.add_device("GPU #" + std::to_string(dev) + " [HIP]", "GPU", dev);
        m_workers.emplace_back(&Miner::gpu_worker_thread, this, dev, BackendType::HIP, gpu_idx);
    }

    for (int dev : m_opencl_devices) {
        size_t gpu_idx = dev_idx++;
        m_stats.add_device("GPU #" + std::to_string(dev) + " [OpenCL]", "GPU", dev);
        m_workers.emplace_back(&Miner::gpu_worker_thread, this, dev, BackendType::OPENCL, gpu_idx);
    }

    // Initial telemetry sample
    update_telemetry();
}

void Miner::update_telemetry() {
    if (!m_hw_monitor) return;

    for (size_t i = 0; i < m_stats.num_devices(); ++i) {
        auto* dev = m_stats.get_device(i);
        if (!dev) continue;

        if (dev->type_label == "GPU") {
            auto tel = m_hw_monitor->get_gpu_telemetry(dev->hw_index);
            m_stats.update_telemetry(i, tel.temp_c, tel.power_w, tel.fan_pct,
                                     tel.has_temp, tel.has_power, tel.has_fan);
            if (!tel.name.empty()) {
                dev->name = tel.name;
            }
        } else if (dev->type_label == "CPU") {
            auto tel = m_hw_monitor->get_cpu_telemetry(m_cpu_threads, static_cast<int>(std::thread::hardware_concurrency()));
            m_stats.update_telemetry(i, tel.temp_c, tel.power_w, tel.fan_pct,
                                     tel.has_temp, tel.has_power, tel.has_fan);
            if (!tel.name.empty()) {
                dev->name = "CPU [" + tel.name + " " + std::to_string(m_cpu_threads) + "T]";
            }
        }
    }
}

void Miner::stop() {
    m_running.store(false);
    for (auto& t : m_workers) {
        if (t.joinable()) {
            t.join();
        }
    }
    m_workers.clear();
}

void Miner::cpu_worker_thread(int thread_id) {
    auto cpu_backend = blake3::select_best_cpu_backend();
    DeviceStats* stats = m_stats.get_device(0);
    
    constexpr uint32_t BATCH_SIZE = 16384;
    std::shared_ptr<MiningJob> active_pool_job = nullptr;
    uint32_t local_roll_count = 0;
    uint32_t current_nonce = 0;
    MiningJob current_cpu_job;

    auto update_cpu_extranonce2 = [&](const MiningJob& base_job, uint32_t roll_idx) {
        current_cpu_job = base_job;
        // Domain tag 0x00..0x7F for CPU devices (thread-specific lower 7 bits)
        uint32_t en2_val = (static_cast<uint32_t>(thread_id & 0x7F) << 24) | (roll_idx & 0x00FFFFFF);
        current_cpu_job.extra_nonce2 = en2_val;

        char en2_hex[9];
        snprintf(en2_hex, sizeof(en2_hex), "%08x", en2_val);
        current_cpu_job.extra_nonce2_hex = en2_hex;

        size_t en2_offset = 144 + base_job.extra_nonce1_size;
        if (en2_offset + 4 <= 176) {
            current_cpu_job.header_template[en2_offset + 0] = static_cast<uint8_t>((en2_val >> 24) & 0xFF);
            current_cpu_job.header_template[en2_offset + 1] = static_cast<uint8_t>((en2_val >> 16) & 0xFF);
            current_cpu_job.header_template[en2_offset + 2] = static_cast<uint8_t>((en2_val >> 8) & 0xFF);
            current_cpu_job.header_template[en2_offset + 3] = static_cast<uint8_t>(en2_val & 0xFF);
        }

        current_cpu_job.block2_words = blake3::prepare_block2_words(current_cpu_job.header_template);
    };

    while (m_running.load(std::memory_order_relaxed)) {
        std::shared_ptr<MiningJob> job;
        {
            std::lock_guard<std::mutex> lock(m_job_mutex);
            job = m_current_job;
        }

        if (!job || job->stale.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        if (job != active_pool_job) {
            active_pool_job = job;
            local_roll_count = 0;
            current_nonce = 0;
            update_cpu_extranonce2(*job, local_roll_count);
        } else {
            if (static_cast<uint64_t>(current_nonce) + BATCH_SIZE >= 0xFFFFFF00ULL) {
                local_roll_count++;
                current_nonce = 0;
                update_cpu_extranonce2(*active_pool_job, local_roll_count);
            }
        }

        uint32_t start_nonce = current_nonce;
        current_nonce += BATCH_SIZE;

        std::optional<Solution> sol;

        if (cpu_backend == BackendType::CPU_AVX512 && blake3::has_avx512_runtime()) {
            sol = blake3::avx512_mine_range(current_cpu_job.midstate.data(), current_cpu_job.block2_words.data(), start_nonce, BATCH_SIZE, current_cpu_job.share_target.data());
        } else if (cpu_backend == BackendType::CPU_AVX2 && blake3::has_avx2_runtime()) {
            sol = blake3::avx2_mine_range(current_cpu_job.midstate.data(), current_cpu_job.block2_words.data(), start_nonce, BATCH_SIZE, current_cpu_job.share_target.data());
        } else {
            sol = blake3::scalar_mine_range(current_cpu_job.midstate.data(), current_cpu_job.block2_words.data(), start_nonce, BATCH_SIZE, current_cpu_job.share_target.data());
        }

        if (stats) {
            stats->hashes.fetch_add(BATCH_SIZE, std::memory_order_relaxed);
        }

        if (sol) {
            sol->device_id = static_cast<uint32_t>(thread_id);
            sol->extra_nonce2 = current_cpu_job.extra_nonce2;
            sol->extra_nonce2_hex = current_cpu_job.extra_nonce2_hex;
            if (stats) {
                stats->accepted.fetch_add(1, std::memory_order_relaxed);
            }
            if (m_solution_cb) {
                m_solution_cb(*sol, current_cpu_job);
            }
        }
    }
}

void Miner::gpu_worker_thread(int device_id, BackendType backend_type, size_t stat_device_idx) {
    DeviceStats* stats = m_stats.get_device(stat_device_idx);
    try {
        std::unique_ptr<GpuBackend> backend;

#if defined(DCR_ENABLE_CUDA)
        if (backend_type == BackendType::CUDA) {
            backend = std::make_unique<CudaBackend>();
        }
#endif
#if defined(DCR_ENABLE_HIP)
        if (backend_type == BackendType::HIP) {
            backend = std::make_unique<HipBackend>();
        }
#endif
#if defined(DCR_ENABLE_OPENCL)
        if (backend_type == BackendType::OPENCL) {
            backend = std::make_unique<OpenCLBackend>();
        }
#endif

        if (!backend || !backend->init(device_id)) {
            if (stats) stats->hw_errors.fetch_add(1, std::memory_order_relaxed);
            dcr::term::log_warn("⚠️ [GPU]", "GPU #" + std::to_string(device_id) + " failed to initialize. Skipping.");
            return;
        }

        if (stats) {
            std::string dev_name = backend->device_name();
            if (!dev_name.empty()) stats->name = dev_name;
        }

        constexpr uint32_t GPU_BATCH_SIZE = 2097152; // 2M nonces per launch (optimal latency vs throughput)
        std::shared_ptr<MiningJob> active_pool_job = nullptr;
        uint32_t local_roll_count = 0;
        uint32_t current_nonce = 0;
        MiningJob current_gpu_job;

        auto update_card_extranonce2 = [&](const MiningJob& base_job, uint32_t roll_idx) {
            current_gpu_job = base_job;
            // Domain tag 0x80..0xFF for GPU devices
            uint32_t en2_val = (static_cast<uint32_t>(0x80 | (device_id & 0x7F)) << 24) | (roll_idx & 0x00FFFFFF);
            current_gpu_job.extra_nonce2 = en2_val;

            char en2_hex[9];
            snprintf(en2_hex, sizeof(en2_hex), "%08x", en2_val);
            current_gpu_job.extra_nonce2_hex = en2_hex;

            size_t en2_offset = 144 + base_job.extra_nonce1_size;
            if (en2_offset + 4 <= 176) {
                current_gpu_job.header_template[en2_offset + 0] = static_cast<uint8_t>((en2_val >> 24) & 0xFF);
                current_gpu_job.header_template[en2_offset + 1] = static_cast<uint8_t>((en2_val >> 16) & 0xFF);
                current_gpu_job.header_template[en2_offset + 2] = static_cast<uint8_t>((en2_val >> 8) & 0xFF);
                current_gpu_job.header_template[en2_offset + 3] = static_cast<uint8_t>(en2_val & 0xFF);
            }

            current_gpu_job.block2_words = blake3::prepare_block2_words(current_gpu_job.header_template);
            backend->set_job(current_gpu_job.midstate, current_gpu_job.block2_words, current_gpu_job.share_target);
        };

        while (m_running.load(std::memory_order_relaxed)) {
            std::shared_ptr<MiningJob> job;
            {
                std::lock_guard<std::mutex> lock(m_job_mutex);
                job = m_current_job;
            }

            if (!job || job->stale.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            if (job != active_pool_job) {
                active_pool_job = job;
                local_roll_count = 0;
                current_nonce = 0;
                update_card_extranonce2(*job, local_roll_count);
            } else {
                // Check if current nonce space is about to be exhausted (4.29 billion nonces)
                if (static_cast<uint64_t>(current_nonce) + GPU_BATCH_SIZE >= 0xFFFFFF00ULL) {
                    local_roll_count++;
                    current_nonce = 0;
                    update_card_extranonce2(*active_pool_job, local_roll_count);
                }
            }

            uint32_t start_nonce = current_nonce;
            current_nonce += GPU_BATCH_SIZE;

            uint64_t hashes_done = backend->mine(start_nonce, GPU_BATCH_SIZE);
            if (stats) {
                stats->hashes.fetch_add(hashes_done, std::memory_order_relaxed);
            }

            auto solutions = backend->get_solutions();
            for (auto& sol : solutions) {
                sol.device_id = static_cast<uint32_t>(stat_device_idx);
                sol.extra_nonce2 = current_gpu_job.extra_nonce2;
                sol.extra_nonce2_hex = current_gpu_job.extra_nonce2_hex;
                if (stats) stats->accepted.fetch_add(1, std::memory_order_relaxed);
                if (m_solution_cb) {
                    m_solution_cb(sol, current_gpu_job);
                }
            }
        }

        backend->shutdown();
    } catch (const std::exception& ex) {
        if (stats) stats->hw_errors.fetch_add(1, std::memory_order_relaxed);
        dcr::term::log_error("🛑 [FAULT ISOLATION]", "GPU #" + std::to_string(device_id) + " encountered error: " + ex.what() + ". Other devices unaffected.");
    } catch (...) {
        if (stats) stats->hw_errors.fetch_add(1, std::memory_order_relaxed);
        dcr::term::log_error("🛑 [FAULT ISOLATION]", "GPU #" + std::to_string(device_id) + " encountered unexpected error. Other devices unaffected.");
    }
}

} // namespace dcr

