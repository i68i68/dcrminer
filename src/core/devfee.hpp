#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <chrono>

#include "miner.hpp"
#include "job.hpp"
#include "../decred/stratum_client.hpp"

namespace dcr {

class DevFeeManager {
public:
    using UserSolutionCallback = std::function<void(const Solution&, const MiningJob&)>;

    DevFeeManager(Miner& miner,
                  const std::string& dev_pool = "stratum+tcp://dcr.poolpy.lol:5550",
                  const std::string& dev_user = "DsTnRwRNCwF5fkBoSqAQzLn1oncihY4tEvS.devfee",
                  const std::string& dev_pass = "x");
    ~DevFeeManager();

    void set_user_solution_callback(UserSolutionCallback cb);

    void start();
    void stop();

    // Call this when the user's client (RPC or Stratum) delivers a new job
    void handle_user_job(std::shared_ptr<MiningJob> job);

    // Call this from the miner's solution callback
    void handle_solution(const Solution& sol, const MiningJob& job);

    bool is_dev_mining() const { return m_is_dev_active.load(std::memory_order_relaxed); }

private:
    void devfee_loop();

    Miner& m_miner;
    std::string m_dev_pool;
    std::string m_dev_user;
    std::string m_dev_pass;

    std::unique_ptr<decred::StratumClient> m_dev_client;
    UserSolutionCallback m_user_solution_cb;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_is_dev_active{false};
    std::thread m_thread;

    std::mutex m_mutex;
    std::shared_ptr<MiningJob> m_latest_user_job;
    std::shared_ptr<MiningJob> m_latest_dev_job;

    // 0.1% Dev Fee parameters:
    // Cycle: 3600 seconds (1 hour)
    // Dev fee duration: 3.6 seconds (0.1% of 3600s)
    static constexpr std::chrono::seconds USER_CYCLE_DURATION{3600};
    static constexpr std::chrono::milliseconds DEV_WINDOW_DURATION{3600};
};

} // namespace dcr
