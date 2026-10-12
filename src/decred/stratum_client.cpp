#include "stratum_client.hpp"
#include <iostream>
#include <chrono>
#include <sstream>
#include <vector>
#include <cstring>
#include <algorithm>

#include "../common/types.hpp"
#include "../decred/header.hpp"
#include "../core/target.hpp"
#include "../common/terminal.hpp"

namespace decred {

static bool hex_to_bytes(const std::string& hex, uint8_t* out, size_t out_len) {
    if (hex.size() < out_len * 2) return false;
    for (size_t i = 0; i < out_len; i++) {
        char h1 = hex[i * 2];
        char h2 = hex[i * 2 + 1];
        auto hex_val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int v1 = hex_val(h1);
        int v2 = hex_val(h2);
        if (v1 < 0 || v2 < 0) return false;
        out[i] = static_cast<uint8_t>((v1 << 4) | v2);
    }
    return true;
}

static std::vector<std::string> parse_json_string_params(const std::string& json_msg) {
    std::vector<std::string> params;
    size_t pos = json_msg.find("\"params\":");
    if (pos == std::string::npos) return params;
    pos = json_msg.find('[', pos);
    if (pos == std::string::npos) return params;

    pos++; // skip '['
    while (pos < json_msg.size()) {
        while (pos < json_msg.size() && (json_msg[pos] == ' ' || json_msg[pos] == '\t' || json_msg[pos] == ',' || json_msg[pos] == '\r' || json_msg[pos] == '\n')) {
            pos++;
        }
        if (pos >= json_msg.size() || json_msg[pos] == ']') break;

        if (json_msg[pos] == '"') {
            pos++;
            std::string item;
            while (pos < json_msg.size() && json_msg[pos] != '"') {
                if (json_msg[pos] == '\\' && pos + 1 < json_msg.size()) {
                    pos++;
                }
                item += json_msg[pos++];
            }
            if (pos < json_msg.size() && json_msg[pos] == '"') pos++;
            params.push_back(item);
        } else if (json_msg[pos] == '[') {
            int depth = 1;
            pos++;
            while (pos < json_msg.size() && depth > 0) {
                if (json_msg[pos] == '[') depth++;
                else if (json_msg[pos] == ']') depth--;
                pos++;
            }
            params.push_back("");
        } else {
            size_t end_item = json_msg.find_first_of(",]", pos);
            if (end_item == std::string::npos) end_item = json_msg.size();
            std::string item = json_msg.substr(pos, end_item - pos);
            while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.pop_back();
            params.push_back(item);
            pos = end_item;
        }
    }
    return params;
}

bool StratumClient::parse_url(const std::string& url, std::string& out_host, std::string& out_port) {
    std::string s = url;
    
    // Strip scheme prefix
    const std::vector<std::string> prefixes = {"stratum+tcp://", "stratum+ssl://", "stratum://", "tcp://"};
    for (const auto& prefix : prefixes) {
        if (s.rfind(prefix, 0) == 0) {
            s = s.substr(prefix.length());
            break;
        }
    }

    // Strip trailing slash if present
    if (!s.empty() && s.back() == '/') {
        s.pop_back();
    }

    size_t colon = s.find(':');
    if (colon != std::string::npos) {
        out_host = s.substr(0, colon);
        out_port = s.substr(colon + 1);
    } else {
        out_host = s;
        out_port = "5550"; // Decred standard dcrpool port
    }

    return !out_host.empty();
}

StratumClient::StratumClient(const std::string& pool_url, const std::string& user, const std::string& pass, const std::string& failover_url)
    : m_pool_url(pool_url), m_user(user), m_pass(pass), m_failover_url(failover_url), m_running(false) {
#ifdef _WIN32
    static bool wsa_inited = false;
    if (!wsa_inited) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        wsa_inited = true;
    }
#endif
}

StratumClient::~StratumClient() {
    stop();
}

void StratumClient::set_work_callback(WorkCallback cb) {
    m_work_cb = cb;
}

void StratumClient::start() {
    if (m_running.load()) return;
    m_running.store(true);
    m_thread = std::thread(&StratumClient::connection_thread, this);
}

void StratumClient::stop() {
    m_running.store(false);
    disconnect_socket();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void StratumClient::disconnect_socket() {
    std::lock_guard<std::mutex> lock(m_send_mutex);
    if (m_socket != DCR_INVALID_SOCKET) {
#ifdef _WIN32
        shutdown(m_socket, SD_BOTH);
        closesocket(m_socket);
#else
        shutdown(m_socket, SHUT_RDWR);
        close(m_socket);
#endif
        m_socket = DCR_INVALID_SOCKET;
        m_authorized.store(false);
    }
}

bool StratumClient::send_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(m_send_mutex);
    if (m_socket == DCR_INVALID_SOCKET) return false;

    std::string data = line;
    if (data.empty() || data.back() != '\n') data += '\n';

    size_t sent = 0;
    while (sent < data.size()) {
        int res = send(m_socket, data.c_str() + sent, static_cast<int>(data.size() - sent), 0);
        if (res <= 0) return false;
        sent += res;
    }
    return true;
}

bool StratumClient::connect_to_pool(const std::string& host, const std::string& port) {
    disconnect_socket();

    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* res = nullptr;
    int err = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
    if (err != 0 || !res) {
        std::string err_msg = "DNS name resolution failed for host \"" + host + "\" (";
#ifdef _WIN32
        err_msg += gai_strerrorA(err);
#else
        err_msg += gai_strerror(err);
#endif
        err_msg += "). Please check the pool address.";
        dcr::term::log_error("❌ [STRATUM]", err_msg);
        return false;
    }

    socket_handle_t sock = DCR_INVALID_SOCKET;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == DCR_INVALID_SOCKET) continue;

        // Set socket timeouts (8 seconds)
#ifdef _WIN32
        DWORD timeout_ms = 8000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
#else
        struct timeval tv{8, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

        if (connect(sock, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) {
            m_socket = sock;
            break;
        }

#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        sock = DCR_INVALID_SOCKET;
    }

    freeaddrinfo(res);

    if (m_socket == DCR_INVALID_SOCKET) {
        dcr::term::log_error("❌ [STRATUM]", "Could not establish TCP connection to " + host + ":" + port + " (Connection refused, unreachable, or port closed).");
        return false;
    }

    dcr::term::log_success("✅ [STRATUM]", "Successfully connected to " + host + ":" + port);
    return true;
}

void StratumClient::connection_thread() {
    int backoff_ms = 1000;
    bool using_failover = false;

    while (m_running.load()) {
        std::string current_pool_url = (using_failover && !m_failover_url.empty()) ? m_failover_url : m_pool_url;
        std::string host, port;
        parse_url(current_pool_url, host, port);

        dcr::term::log_info("🌐 [STRATUM]", "Connecting to " + host + ":" + port + "...");

        if (connect_to_pool(host, port)) {
            backoff_ms = 1000; // Reset backoff on success

            // 1. Send mining.subscribe
            std::string sub_req = "{\"id\": 1, \"method\": \"mining.subscribe\", \"params\": [\"dcrminer/0.1.0\"]}\n";
            send_line(sub_req);

            // 2. Send mining.authorize
            std::string auth_req = "{\"id\": 2, \"method\": \"mining.authorize\", \"params\": [\"" + m_user + "\", \"" + m_pass + "\"]}\n";
            send_line(auth_req);

            std::string line_buffer;
            char recv_buf[4096];

            while (m_running.load()) {
                int bytes = recv(m_socket, recv_buf, sizeof(recv_buf) - 1, 0);
                if (bytes <= 0) {
#ifdef _WIN32
                    int wsa_err = WSAGetLastError();
                    if (wsa_err == WSAETIMEDOUT) {
                        continue;
                    }
#else
                    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                        continue;
                    }
#endif
                    dcr::term::log_warn("⚠️ [STRATUM]", "Server closed connection or connection timed out.");
                    break;
                }

                recv_buf[bytes] = '\0';
                line_buffer.append(recv_buf, bytes);

                size_t newline_pos = 0;
                while ((newline_pos = line_buffer.find('\n')) != std::string::npos) {
                    std::string line = line_buffer.substr(0, newline_pos);
                    line_buffer.erase(0, newline_pos + 1);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (!line.empty()) {
                        process_message(line);
                    }
                }
            }
            disconnect_socket();
        }

        if (!m_running.load()) break;

        std::string retry_msg = "Disconnected. Retrying in " + std::to_string(backoff_ms / 1000) + "s...";
        dcr::term::log_warn("🔄 [STRATUM]", retry_msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        backoff_ms = std::min(backoff_ms * 2, 30000);

        if (!m_failover_url.empty()) {
            using_failover = !using_failover;
        }
    }
}

static inline bool json_has_key_val(const std::string& msg, const std::string& key, const std::string& val) {
    if (msg.find("\"" + key + "\":" + val) != std::string::npos) return true;
    if (msg.find("\"" + key + "\": " + val) != std::string::npos) return true;
    if (msg.find("\"" + key + "\" :" + val) != std::string::npos) return true;
    if (msg.find("\"" + key + "\" : " + val) != std::string::npos) return true;
    return false;
}

static inline bool json_has_key(const std::string& msg, const std::string& key) {
    if (msg.find("\"" + key + "\":") != std::string::npos) return true;
    if (msg.find("\"" + key + "\" :") != std::string::npos) return true;
    return false;
}

void StratumClient::process_message(const std::string& msg) {
    // Check for subscribe result (id: 1)
    if (json_has_key_val(msg, "id", "1") && json_has_key(msg, "result")) {
        size_t res_pos = msg.find("\"result\":");
        if (res_pos != std::string::npos) {
            size_t close_bracket = msg.find("],\"", res_pos);
            if (close_bracket == std::string::npos) close_bracket = msg.find("], \"", res_pos);
            if (close_bracket != std::string::npos) {
                size_t q1 = msg.find('"', close_bracket + 2);
                if (q1 != std::string::npos) {
                    size_t q2 = msg.find('"', q1 + 1);
                    if (q2 != std::string::npos) {
                        m_extranonce1 = msg.substr(q1 + 1, q2 - (q1 + 1));
                        dcr::term::log_info("🔑 [STRATUM]", "Pool assigned ExtraNonce1: " + m_extranonce1);
                    }
                }
            }
        }
    }

    // Check for authorization result (id: 2)
    if (json_has_key_val(msg, "id", "2")) {
        if (json_has_key_val(msg, "result", "true")) {
            m_authorized.store(true);
            if (m_is_devfee) {
                dcr::term::log_success("🔐 [DEVFEE]", "DevFee worker connected & authorized (0.1% support pool).");
            } else {
                dcr::term::log_success("🔐 [STRATUM]", "Worker successfully authorized (" + m_user + ")!");
            }
        } else if (json_has_key(msg, "error") && !json_has_key_val(msg, "error", "null")) {
            m_authorized.store(false);
            if (!m_is_devfee) {
                dcr::term::log_error("❌ [STRATUM]", "Worker authorization rejected! Response: " + msg);
            }
        }
    }

    // Check for share submission result (id >= 3)
    if (json_has_key(msg, "id") && !json_has_key_val(msg, "id", "null") && !json_has_key_val(msg, "id", "1") && !json_has_key_val(msg, "id", "2")) {
        if (json_has_key_val(msg, "result", "true")) {
            if (m_is_devfee) {
                dcr::term::log_success("✅ [DEVFEE]", "0.1% DevFee share accepted by pool! Thank you for supporting development! 🎉");
            } else {
                dcr::term::log_success("✅ [STRATUM]", "Share accepted by pool! 🎉");
            }
        } else if (json_has_key(msg, "error") && !json_has_key_val(msg, "error", "null")) {
            if (!m_is_devfee) {
                dcr::term::log_warn("⚠️ [STRATUM]", "Share rejected by pool: " + msg);
            }
        }
    }

    if (msg.find("mining.notify") != std::string::npos) {
        handle_mining_notify(msg);
    } else if (msg.find("mining.set_difficulty") != std::string::npos) {
        handle_set_difficulty(msg);
    }
}

void StratumClient::handle_set_difficulty(const std::string& msg) {
    size_t params_pos = msg.find("\"params\":[");
    if (params_pos != std::string::npos) {
        size_t start = params_pos + 10;
        size_t end = msg.find_first_of(",]", start);
        if (end != std::string::npos) {
            std::string diff_str = msg.substr(start, end - start);
            try {
                m_difficulty = std::stod(diff_str);
                if (!m_is_devfee) {
                    dcr::term::log_info("🎯 [STRATUM]", "Pool set target difficulty: " + diff_str);
                }
            } catch (...) {}
        }
    }
}

void StratumClient::handle_mining_notify(const std::string& msg) {
    auto params = parse_json_string_params(msg);
    std::string job_id_str = params.empty() ? std::to_string(++m_job_counter) : params[0];

    if (!m_is_devfee) {
        dcr::term::log_info("📥 [STRATUM]", "Received new mining job from pool! (Job ID: " + job_id_str + ")");
    }
    
    dcr::HeaderBytes header_bytes{};
    dcr::DcrHeader h{};
    h.version = 1;
    h.bits = 0x1d00ffff;
    h.size = 180;
    h.timestamp = static_cast<uint32_t>(std::time(nullptr));
    h.serialize(header_bytes);

    bool loaded = false;
    for (const auto& p : params) {
        if (p.size() == 360) {
            hex_to_bytes(p, header_bytes.data(), 180);
            loaded = true;
            break;
        }
    }

    if (!loaded && params.size() >= 3) {
        // Standard Decred Stratum:
        // params[1]: PrevBlock (32 bytes = 64 hex chars)
        // params[2]: MidHeader (100 bytes = 200 hex chars)
        if (params[1].size() >= 64) {
            hex_to_bytes(params[1], header_bytes.data() + 4, 32);
        }
        if (params[2].size() >= 200) {
            size_t copy_len = std::min<size_t>(params[2].size() / 2, 100);
            hex_to_bytes(params[2], header_bytes.data() + 36, copy_len);
        }
        if (params.size() > 3 && params[3].size() >= 8) { // StakeVersion
            hex_to_bytes(params[3], header_bytes.data() + 176, 4);
        }
        if (params.size() > 5 && params[5].size() >= 8) { // Version
            hex_to_bytes(params[5], header_bytes.data() + 0, 4);
        }
        if (params.size() > 7 && params[7].size() >= 8) { // NTime / Timestamp
            hex_to_bytes(params[7], header_bytes.data() + 136, 4);
        }
    }

    if (!m_extranonce1.empty()) {
        size_t en1_bytes = std::min<size_t>(m_extranonce1.size() / 2, 16);
        hex_to_bytes(m_extranonce1, header_bytes.data() + 144, en1_bytes);
        // Ensure ExtraNonce2 region (offset 144 + en1_bytes) is initialized to zero to match pool default
        std::memset(header_bytes.data() + 144 + en1_bytes, 0, 32 - en1_bytes);
    }

    uint32_t bits = dcr::DcrHeader::get_bits(header_bytes);
    if (bits == 0) bits = 0x1d00ffff;
    dcr::Target256 net_target = dcr::compact_to_target(bits);
    dcr::Target256 share_target = (m_difficulty > 0.0) ? dcr::difficulty_to_target(m_difficulty) : net_target;

    auto job = std::make_shared<dcr::MiningJob>(dcr::MiningJob::from_header(header_bytes, share_target, net_target));
    job->stratum_job_id = job_id_str;
    try {
        job->job_id = std::stoull(job_id_str, nullptr, 16);
    } catch (...) {
        job->job_id = ++m_job_counter;
    }
    job->is_devfee = m_is_devfee;
    if (!m_extranonce1.empty()) {
        job->extra_nonce1_size = std::min<size_t>(m_extranonce1.size() / 2, 16);
    }

    if (m_work_cb) {
        m_work_cb(job);
    }
}

bool StratumClient::submit_work(const std::string& job_id, const std::string& extranonce2, const std::string& ntime, const std::string& nonce) {
    uint64_t req_id = ++m_msg_id;
    std::string en2 = extranonce2.empty() ? "00000000" : extranonce2;
    std::string nt = ntime.empty() ? "00000000" : ntime;

    std::string nonce_hex = nonce;
    try {
        uint32_t n_val = std::stoul(nonce);
        char n_buf[16];
        snprintf(n_buf, sizeof(n_buf), "%08x", n_val);
        nonce_hex = n_buf;
    } catch (...) {}

    std::ostringstream ss;
    ss << "{\"id\":" << req_id << ",\"method\":\"mining.submit\",\"params\":[\""
       << m_user << "\",\"" << job_id << "\",\"" << en2 << "\",\"" << nt << "\",\"" << nonce_hex << "\"]}\n";

    if (m_is_devfee) {
        dcr::term::log_info("📤 [DEVFEE]", "Submitting DevFee share (Job: " + job_id + ", Nonce: 0x" + nonce_hex + ")");
    } else {
        dcr::term::log_info("📤 [STRATUM]", "Submitting share for job " + job_id + " (Nonce: 0x" + nonce_hex + ")");
    }
    return send_line(ss.str());
}

} // namespace decred
