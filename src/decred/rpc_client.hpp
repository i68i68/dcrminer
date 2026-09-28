#pragma once

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
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
using rpc_socket_t = SOCKET;
#define DCR_RPC_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
using rpc_socket_t = int;
#define DCR_RPC_INVALID_SOCKET (-1)
#endif

#include "../core/job.hpp"

namespace decred {

class RpcClient {
public:
    using WorkCallback = std::function<void(std::shared_ptr<dcr::MiningJob>)>;

    RpcClient(const std::string& host, const std::string& user, const std::string& pass, const std::string& cert_path);
    ~RpcClient();

    void set_work_callback(WorkCallback cb);
    
    void start();
    void stop();

    bool submit_work(const std::string& header_hex);

private:
    void polling_thread();
    std::shared_ptr<dcr::MiningJob> fetch_work();
    std::string http_post(const std::string& req_json);

    std::string m_host;
    std::string m_user;
    std::string m_pass;
    std::string m_cert_path;

    std::string m_rpc_host;
    std::string m_rpc_port;

    WorkCallback m_work_cb;
    
    std::atomic<bool> m_running{false};
    std::thread m_thread;
    std::mutex m_rpc_mutex;
    uint64_t m_job_counter = 0;
};

} // namespace decred
