#include "devfee.hpp"
#include "../common/terminal.hpp"
#include <iostream>

namespace dcr {

DevFeeManager::DevFeeManager(Miner& miner,
                             const std::string& dev_pool,
                             const std::string& dev_user,
                             const std::string& dev_pass)
    : m_miner(miner),
      m_dev_pool(dev_pool),
      m_dev_user(dev_user),
      m_dev_pass(dev_pass) {
    m_dev_client = std::make_unique<decred::StratumClient>(m_dev_pool, m_dev_user, m_dev_pass);
    m_dev_client->set_devfee_mode(true);

    m_dev_client->set_work_callback([this](std::shared_ptr<MiningJob> job) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latest_dev_job = job;
        if (m_is_dev_active.load(std::memory_order_relaxed)) {
            m_miner.update_job(job);
        }
    });
}

DevFeeManager::~DevFeeManager() {
    stop();
}

void DevFeeManager::set_user_solution_callback(UserSolutionCallback cb) {
    m_user_solution_cb = std::move(cb);
}

void DevFeeManager::start() {
    if (m_running.load()) return;
    m_running.store(true);
    
    if (m_dev_client) {
        m_dev_client->start();
    }
    m_thread = std::thread(&DevFeeManager::devfee_loop, this);
}

void DevFeeManager::stop() {
    if (!m_running.load()) return;
    m_running.store(false);

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_dev_client) {
        m_dev_client->stop();
    }
}

void DevFeeManager::handle_user_job(std::shared_ptr<MiningJob> job) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latest_user_job = job;
    }
    if (!m_is_dev_active.load(std::memory_order_relaxed)) {
        m_miner.update_job(job);
    }
}

void DevFeeManager::handle_solution(const Solution& sol, const MiningJob& job) {
    if (job.is_devfee) {
        if (m_dev_client) {
            std::string job_id_str = job.stratum_job_id.empty() ? std::to_string(job.job_id) : job.stratum_job_id;
            std::string en2 = sol.extra_nonce2_hex.empty() ? (job.extra_nonce2_hex.empty() ? "00000000" : job.extra_nonce2_hex) : sol.extra_nonce2_hex;
            m_dev_client->submit_work(job_id_str, en2, "", std::to_string(sol.nonce));
        }
    } else {
        if (m_user_solution_cb) {
            m_user_solution_cb(sol, job);
        }
    }
}

void DevFeeManager::devfee_loop() {
    // Initial warmup delay (10 minutes) before the first dev fee slice,
    // ensuring user has immediate, uninterrupted startup.
    constexpr auto INITIAL_WARMUP = std::chrono::seconds(600);
    auto warmup_start = std::chrono::steady_clock::now();
    while (m_running.load()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - warmup_start);
        if (elapsed >= INITIAL_WARMUP) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    while (m_running.load()) {
        // Sleep user cycle (1 hour = 3600 seconds) in 500ms increments for responsive shutdown
        auto start_wait = std::chrono::steady_clock::now();
        while (m_running.load()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_wait);
            if (elapsed >= USER_CYCLE_DURATION) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        if (!m_running.load()) break;

        // Check if dev pool is connected, authorized, and has a valid job
        std::shared_ptr<MiningJob> dev_job = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_dev_client && m_dev_client->is_authorized() && m_latest_dev_job && !m_latest_dev_job->stale.load()) {
                dev_job = m_latest_dev_job;
            }
        }

        if (!dev_job) {
            // Dev pool is unreachable or not ready; fail-safe fallback: continue user mining 100% uninterrupted
            continue;
        }

        // Switch to 0.1% Dev Fee window (3.6 seconds)
        term::log_info("⚡ [DEVFEE]", "Initiating 0.1% dev fee time-slice (3.6s)...");
        m_is_dev_active.store(true, std::memory_order_release);
        m_miner.update_job(dev_job);

        auto start_dev = std::chrono::steady_clock::now();
        while (m_running.load()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_dev);
            if (elapsed >= DEV_WINDOW_DURATION) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Switch back to user job immediately
        m_is_dev_active.store(false, std::memory_order_release);
        std::shared_ptr<MiningJob> user_job = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            user_job = m_latest_user_job;
        }

        if (user_job) {
            m_miner.update_job(user_job);
        }
        term::log_info("⛏️  [DEVFEE]", "Dev fee cycle complete. Resumed user mining 100%.");
    }
}

} // namespace dcr
