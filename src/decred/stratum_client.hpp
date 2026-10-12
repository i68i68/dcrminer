#pragma once

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_handle_t = SOCKET;
#define DCR_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
using socket_handle_t = int;
#define DCR_INVALID_SOCKET (-1)
#endif

#include "../core/job.hpp"

namespace decred {

class StratumClient {
public:
    using WorkCallback = std::function<void(std::shared_ptr<dcr::MiningJob>)>;

    StratumClient(const std::string& pool_url, const std::string& user, const std::string& pass, const std::string& failover_url = "");
    ~StratumClient();

    void set_work_callback(WorkCallback cb);
    
    void start();
    void stop();

    bool submit_work(const std::string& job_id, const std::string& extranonce2, const std::string& ntime, const std::string& nonce);

    bool is_authorized() const { return m_authorized.load(); }
    void set_devfee_mode(bool enable) { m_is_devfee = enable; }
    bool is_devfee_mode() const { return m_is_devfee; }

    static bool parse_url(const std::string& url, std::string& out_host, std::string& out_port);

private:
    void connection_thread();
    bool connect_to_pool(const std::string& host, const std::string& port);
    void disconnect_socket();
    bool send_line(const std::string& line);
    void process_message(const std::string& msg);
    void handle_mining_notify(const std::string& msg);
    void handle_set_difficulty(const std::string& msg);

    std::string m_pool_url;
    std::string m_user;
    std::string m_pass;
    std::string m_failover_url;

    WorkCallback m_work_cb;
    
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_authorized{false};
    bool m_is_devfee{false};
    std::thread m_thread;
    std::mutex m_send_mutex;
    
    socket_handle_t m_socket = DCR_INVALID_SOCKET;
    uint64_t m_msg_id = 100; // Offset above reserved IDs (1=subscribe, 2=authorize)

    std::string m_extranonce1;
    size_t m_extranonce2_size = 4;
    double m_difficulty = 1.0;
    uint64_t m_job_counter = 0;
};

} // namespace decred
