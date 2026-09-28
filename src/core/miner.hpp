#pragma once

#include <atomic>
#include <vector>
#include <thread>
#include <memory>
#include <mutex>
#include <cstdint>
#include <functional>
#include "../common/types.hpp"
#include "job.hpp"
#include "stats.hpp"
#include "nonce_allocator.hpp"
#include "../gpu/gpu_backend.hpp"

namespace dcr {

namespace telemetry {
class HardwareMonitor;
}

class Miner {
public:
    using SolutionCallback = std::function<void(const Solution&, const MiningJob&)>;

    Miner();
    ~Miner();

    // Configuration
    void set_cpu_threads(int threads); // 0 = auto
    void enable_cuda(const std::vector<int>& devices);
    void enable_hip(const std::vector<int>& devices);
    void enable_opencl(const std::vector<int>& devices);

    void set_solution_callback(SolutionCallback cb) { m_solution_cb = std::move(cb); }

    // Job management
    void update_job(std::shared_ptr<MiningJob> new_job);
    
    // Lifecycle
    void start();
    void stop();

    // Statistics
    MinerStats& get_stats() { return m_stats; }
    const MinerStats& get_stats() const { return m_stats; }

    uint64_t get_total_hashes() const;
    void update_telemetry();

private:
    void cpu_worker_thread(int thread_id);
    void gpu_worker_thread(int device_id, BackendType backend_type, size_t stat_device_idx);

    std::atomic<bool> m_running{false};
    std::shared_ptr<MiningJob> m_current_job;
    std::mutex m_job_mutex;

    int m_cpu_threads = 0;
    std::vector<int> m_cuda_devices;
    std::vector<int> m_hip_devices;
    std::vector<int> m_opencl_devices;

    std::vector<std::thread> m_workers;
    
    NonceAllocator m_nonce_alloc;
    MinerStats m_stats;
    SolutionCallback m_solution_cb;

    std::unique_ptr<telemetry::HardwareMonitor> m_hw_monitor;
};

} // namespace dcr
