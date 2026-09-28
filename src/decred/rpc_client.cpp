#include "rpc_client.hpp"
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
#include "../blake3/blake3_ref.hpp"
#include <fstream>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace decred {

static bool parse_http_url(const std::string& url, std::string& out_host, std::string& out_port) {
    std::string s = url;
    if (s.rfind("http://", 0) == 0) s = s.substr(7);
    else if (s.rfind("https://", 0) == 0) s = s.substr(8);

    if (!s.empty() && s.back() == '/') s.pop_back();

    size_t colon = s.find(':');
    if (colon != std::string::npos) {
        out_host = s.substr(0, colon);
        out_port = s.substr(colon + 1);
    } else {
        out_host = s;
        out_port = "9109"; // Default dcrd RPC port
    }
    return !out_host.empty();
}

static std::string base64_encode(const std::string& in) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (uint8_t c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

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

RpcClient::RpcClient(const std::string& host, const std::string& user, const std::string& pass, const std::string& cert_path)
    : m_host(host), m_user(user), m_pass(pass), m_cert_path(cert_path), m_running(false) {
#ifdef _WIN32
    static bool wsa_inited = false;
    if (!wsa_inited) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        wsa_inited = true;
    }
#endif
    parse_http_url(m_host, m_rpc_host, m_rpc_port);
}

RpcClient::~RpcClient() {
    stop();
}

void RpcClient::set_work_callback(WorkCallback cb) {
    m_work_cb = cb;
}

void RpcClient::start() {
    if (m_running.load()) return;
    m_running.store(true);
    dcr::term::log_info("🌐 [SOLO RPC]", "Connecting to dcrd RPC at " + m_rpc_host + ":" + m_rpc_port + "...");
    m_thread = std::thread(&RpcClient::polling_thread, this);
}

void RpcClient::stop() {
    m_running.store(false);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

std::string RpcClient::http_post(const std::string& req_json) {
#ifdef _WIN32
    std::string resp;
    std::wstring w_host(m_rpc_host.begin(), m_rpc_host.end());
    int port = 9109;
    try { port = std::stoi(m_rpc_port); } catch (...) {}

    HINTERNET hSession = WinHttpOpen(L"dcrminer/0.1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, w_host.c_str(), static_cast<INTERNET_PORT>(port), 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }

    bool is_https = (m_host.rfind("https://", 0) == 0) || (m_host.rfind("http://", 0) != 0) || (port == 9109);
    DWORD req_flags = is_https ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/", NULL,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           req_flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    if (is_https) {
        DWORD sec_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                          SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                          SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                          SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &sec_flags, sizeof(sec_flags));
    }

    if (!m_user.empty()) {
        std::wstring w_user(m_user.begin(), m_user.end());
        std::wstring w_pass(m_pass.begin(), m_pass.end());
        WinHttpSetCredentials(hRequest, WINHTTP_AUTH_TARGET_SERVER, WINHTTP_AUTH_SCHEME_BASIC,
                              w_user.c_str(), w_pass.c_str(), NULL);
    }

    LPCWSTR headers = L"Content-Type: application/json\r\n";
    WinHttpAddRequestHeaders(hRequest, headers, -1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    BOOL bResults = WinHttpSendRequest(hRequest,
                                       WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       (LPVOID)req_json.c_str(), static_cast<DWORD>(req_json.size()),
                                       static_cast<DWORD>(req_json.size()), 0);

    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }

    if (bResults) {
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;

            std::vector<char> buffer(dwSize + 1);
            if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                resp.append(buffer.data(), dwDownloaded);
            }
        } while (dwSize > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return resp;
#else
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* res = nullptr;
    int err = getaddrinfo(m_rpc_host.c_str(), m_rpc_port.c_str(), &hints, &res);
    if (err != 0 || !res) {
        return "";
    }

    rpc_socket_t sock = DCR_RPC_INVALID_SOCKET;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == DCR_RPC_INVALID_SOCKET) continue;

        struct timeval tv{4, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(sock, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) {
            break;
        }

        close(sock);
        sock = DCR_RPC_INVALID_SOCKET;
    }

    freeaddrinfo(res);
    if (sock == DCR_RPC_INVALID_SOCKET) {
        return "";
    }

    // Build HTTP POST request
    std::string auth_header = base64_encode(m_user + ":" + m_pass);
    std::ostringstream ss;
    ss << "POST / HTTP/1.1\r\n"
       << "Host: " << m_rpc_host << ":" << m_rpc_port << "\r\n"
       << "Authorization: Basic " << auth_header << "\r\n"
       << "Content-Type: application/json\r\n"
       << "Content-Length: " << req_json.size() << "\r\n"
       << "Connection: close\r\n"
       << "\r\n"
       << req_json;

    std::string req = ss.str();
    size_t sent = 0;
    while (sent < req.size()) {
        int r = send(sock, req.c_str() + sent, static_cast<int>(req.size() - sent), 0);
        if (r <= 0) {
            close(sock);
            return "";
        }
        sent += r;
    }

    // Read full HTTP response
    std::string resp;
    char buf[4096];
    while (true) {
        int r = recv(sock, buf, sizeof(buf), 0);
        if (r <= 0) break;
        resp.append(buf, r);
    }

    close(sock);

    // Split HTTP headers and body
    size_t body_pos = resp.find("\r\n\r\n");
    if (body_pos != std::string::npos) {
        return resp.substr(body_pos + 4);
    }
    return resp;
#endif
}

std::shared_ptr<dcr::MiningJob> RpcClient::fetch_work() {
    std::string req = "{\"jsonrpc\":\"1.0\",\"id\":\"dcrminer\",\"method\":\"getwork\",\"params\":[]}";
    std::string body = http_post(req);
    if (body.empty()) {
        return nullptr;
    }

    // Look for "data":"<360 hex>"
    size_t data_key = body.find("\"data\":");
    if (data_key == std::string::npos) {
        return nullptr;
    }

    size_t q1 = body.find('"', data_key + 7);
    if (q1 == std::string::npos) return nullptr;
    size_t q2 = body.find('"', q1 + 1);
    if (q2 == std::string::npos) return nullptr;

    std::string hex_data = body.substr(q1 + 1, q2 - q1 - 1);
    if (hex_data.size() < 360) {
        return nullptr;
    }

    dcr::HeaderBytes header_bytes{};
    if (!hex_to_bytes(hex_data, header_bytes.data(), 180)) {
        return nullptr;
    }

    // Target parsing
    dcr::Target256 net_target{};
    bool target_parsed = false;
    size_t target_key = body.find("\"target\":");
    if (target_key != std::string::npos) {
        size_t tq1 = body.find('"', target_key + 9);
        if (tq1 != std::string::npos) {
            size_t tq2 = body.find('"', tq1 + 1);
            if (tq2 != std::string::npos) {
                std::string target_hex = body.substr(tq1 + 1, tq2 - tq1 - 1);
                if (target_hex.size() >= 64) {
                    uint8_t target_bytes[32];
                    if (hex_to_bytes(target_hex, target_bytes, 32)) {
                        for (int i = 0; i < 8; i++) {
                            net_target[i] = dcr::read_le32(target_bytes + i * 4);
                        }
                        target_parsed = true;
                    }
                }
            }
        }
    }

    if (!target_parsed) {
        uint32_t bits = dcr::DcrHeader::get_bits(header_bytes);
        net_target = dcr::compact_to_target(bits);
    }

    auto job = std::make_shared<dcr::MiningJob>(dcr::MiningJob::from_header(header_bytes, net_target, net_target));
    job->job_id = ++m_job_counter;
    return job;
}

bool RpcClient::submit_work(const std::string& header_hex) {
    dcr::term::log_info("📤 [SOLO RPC]", "Submitting solved block candidate to dcrd (" + std::to_string(header_hex.size() / 2) + " bytes)...");

    std::string req = "{\"jsonrpc\":\"1.0\",\"id\":\"dcrminer\",\"method\":\"getwork\",\"params\":[\"" + header_hex + "\"]}";
    std::string body = http_post(req);

    if (body.find("\"result\":true") != std::string::npos) {
        // Play console chime
        std::cout << "\a";

        uint8_t raw_header[180] = {0};
        uint32_t height = 0;
        uint32_t nonce = 0;
        std::string hash_hex = "N/A";

        if (hex_to_bytes(header_hex, raw_header, 180)) {
            height = dcr::read_le32(raw_header + dcr::HeaderOffsets::HEIGHT);
            nonce = dcr::read_le32(raw_header + dcr::HeaderOffsets::NONCE);
            auto h = dcr::blake3::blake3_hash(raw_header, 180);
            hash_hex = dcr::hex_encode(h.data(), 32);
        }

        std::time_t t = std::time(nullptr);
        char time_buf[64] = {0};
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

        std::cout << "\n" << dcr::term::BRIGHT_YELLOW << dcr::term::BOLD
                  << "╔══════════════════════════════════════════════════════════════════════════════════════════╗\n"
                  << "║  👑👑👑 DECRED BLOCK FOUND AND ACCEPTED BY NETWORK CONSENSUS! 👑👑👑                    ║\n"
                  << "╠══════════════════════════════════════════════════════════════════════════════════════════╣\n"
                  << "║  📦 Block Height:   #" << height << "\n"
                  << "║  🔑 Solved Nonce:   0x" << std::hex << nonce << std::dec << " (" << nonce << ")\n"
                  << "║  ⚡ PoW Hash:       " << hash_hex << "\n"
                  << "║  💰 Reward Payout:  Sent to miningaddr (Trezor Hardware Wallet)\n"
                  << "║  💾 Saved to Files: blocks_found.log & rewards.csv\n"
                  << "╚══════════════════════════════════════════════════════════════════════════════════════════╝\n"
                  << dcr::term::RESET << "\n";

        // Save detailed human-readable log to blocks_found.log
        try {
            std::ofstream log_file("blocks_found.log", std::ios::app);
            if (log_file.is_open()) {
                log_file << "========================================================================================\n"
                         << "[" << time_buf << "] 👑 DECRED BLOCK #" << height << " FOUND & CONFIRMED!\n"
                         << "  - Timestamp:     " << time_buf << "\n"
                         << "  - Block Height:  " << height << "\n"
                         << "  - Solved Nonce:  0x" << std::hex << nonce << std::dec << " (" << nonce << ")\n"
                         << "  - PoW Hash:      " << hash_hex << "\n"
                         << "  - Solved Header: " << header_hex << "\n"
                         << "  - Network:       Decred Mainnet (dcrd 127.0.0.1:9109)\n"
                         << "  - Status:        ACCEPTED BY dcrd (Coinbase reward sent to Trezor)\n"
                         << "========================================================================================\n\n";
                log_file.flush();
            }
        } catch (...) {}

        // Save structured CSV record to rewards.csv
        try {
            bool exists = false;
            {
                std::ifstream check("rewards.csv");
                exists = check.good();
            }
            std::ofstream csv_file("rewards.csv", std::ios::app);
            if (csv_file.is_open()) {
                if (!exists) {
                    csv_file << "Timestamp,BlockHeight,NonceHex,NonceDecimal,BlockHash,Status\n";
                }
                csv_file << time_buf << ","
                         << height << ","
                         << "0x" << std::hex << nonce << std::dec << ","
                         << nonce << ","
                         << hash_hex << ","
                         << "Accepted\n";
                csv_file.flush();
            }
        } catch (...) {}

        return true;
    } else {
        dcr::term::log_warn("⚠️ [SOLO RPC]", "Submission response: " + body);
        return false;
    }
}

void RpcClient::polling_thread() {
    int backoff_ms = 1000;
    std::string last_header_prev;

    while (m_running.load()) {
        auto job = fetch_work();
        if (job) {
            backoff_ms = 1000; // Reset on success

            // Check if block changed
            std::string prev_hex = dcr::hex_encode(job->header_template.data() + 4, 32);
            if (prev_hex != last_header_prev) {
                last_header_prev = prev_hex;
                dcr::term::log_info("📥 [SOLO RPC]", "New block template received! (Job #" + std::to_string(job->job_id) + ", Prev: " + prev_hex.substr(0, 16) + "...)");
                if (m_work_cb) {
                    m_work_cb(job);
                }
            }

            // Poll every 1.5 seconds for new blocks
            for (int i = 0; i < 15 && m_running.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } else {
            dcr::term::log_warn("⚠️ [SOLO RPC]", "Failed to fetch work from dcrd (" + m_rpc_host + ":" + m_rpc_port + "). Retrying in " + std::to_string(backoff_ms / 1000) + "s...");
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            backoff_ms = std::min(backoff_ms * 2, 20000);
        }
    }
}

} // namespace decred
