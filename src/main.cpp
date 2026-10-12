#include <iostream>
#include <string>
#include <vector>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <iomanip>

#include "core/miner.hpp"
#include "blake3/cpu_features.hpp"
#include "decred/rpc_client.hpp"
#include "decred/stratum_client.hpp"
#include "tuning/autotuner.hpp"
#include "common/terminal.hpp"

#if defined(DCR_ENABLE_CUDA)
#include "gpu/cuda/cuda_backend.hpp"
#endif

#if defined(DCR_ENABLE_OPENCL)
#include "gpu/opencl/opencl_backend.hpp"
#endif

#include "core/devfee.hpp"

std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    std::cout << "\n";
    dcr::term::log_warn("🛑 [SHUTDOWN]", "Signal (" + std::to_string(signum) + ") received. Initiating graceful shutdown...");
    g_running = false;
}

void list_devices() {
    dcr::term::print_banner();

    std::cout << dcr::term::BRIGHT_CYAN << dcr::term::BOLD
              << "╔══════════════════════════════════════════════════════════════════════════════════════════╗\n"
              << "║                       🔍 DETECTED SYSTEM HARDWARE OVERVIEW                               ║\n"
              << "╚══════════════════════════════════════════════════════════════════════════════════════════╝\n"
              << dcr::term::RESET;

    auto features = dcr::blake3::detect_cpu_features();
    unsigned int logical_cores = std::thread::hardware_concurrency();

    std::string cpu_brand = dcr::blake3::get_cpu_brand_string();
    std::cout << "\n" << dcr::term::BRIGHT_YELLOW << dcr::term::BOLD << "💻 [CPU Architecture: " << cpu_brand << "]" << dcr::term::RESET << "\n";
    std::cout << "   ├─ Processor Model:    " << dcr::term::BRIGHT_WHITE << dcr::term::BOLD << cpu_brand << dcr::term::RESET << "\n";
    std::cout << "   ├─ Logical Threads:    " << dcr::term::BRIGHT_WHITE << logical_cores << dcr::term::RESET << "\n";
    std::cout << "   ├─ SSE4.1 Vector:      " << (features.sse41 ? (dcr::term::BRIGHT_GREEN + std::string("✔ YES")) : (dcr::term::DIM + std::string("✖ NO"))) << dcr::term::RESET << "\n";
    std::cout << "   ├─ AVX2 (256-bit SIMD):" << (features.avx2 ? (dcr::term::BRIGHT_GREEN + std::string("✔ YES (8 Lanes parallel)")) : (dcr::term::DIM + std::string("✖ NO"))) << dcr::term::RESET << "\n";
    std::cout << "   ├─ AVX-512 (512-bit):  " << (features.avx512f ? (dcr::term::BRIGHT_GREEN + std::string("✔ YES (16 Lanes parallel)")) : (dcr::term::DIM + std::string("✖ NO"))) << dcr::term::RESET << "\n";
    std::cout << "   └─ Active CPU Backend: " << dcr::term::BRIGHT_CYAN << dcr::term::BOLD << dcr::backend_name(dcr::blake3::select_best_cpu_backend()) << dcr::term::RESET << "\n";

#if defined(DCR_ENABLE_CUDA)
    std::cout << "\n" << dcr::term::BRIGHT_GREEN << dcr::term::BOLD << "🟢 [NVIDIA CUDA Compute Devices]" << dcr::term::RESET << "\n";
    int cuda_count = 0;
    if (cudaGetDeviceCount(&cuda_count) == cudaSuccess && cuda_count > 0) {
        for (int i = 0; i < cuda_count; ++i) {
            cudaDeviceProp prop;
            cudaGetDeviceProperties(&prop, i);
            std::cout << "   ├─ GPU #" << i << ": " << dcr::term::BRIGHT_WHITE << dcr::term::BOLD << prop.name << dcr::term::RESET << "\n"
                      << "   │  ├─ Compute Capability: SM " << prop.major << "." << prop.minor << "\n"
                      << "   │  └─ Dedicated VRAM:     " << (prop.totalGlobalMem / (1024 * 1024)) << " MB\n";
        }
    } else {
        std::cout << "   └─ " << dcr::term::DIM << "No CUDA devices detected or driver not loaded." << dcr::term::RESET << "\n";
    }
#endif

#if defined(DCR_ENABLE_OPENCL)
    std::cout << "\n" << dcr::term::BRIGHT_MAGENTA << dcr::term::BOLD << "🔴 [AMD / Intel / OpenCL Devices]" << dcr::term::RESET << "\n";
    auto ocl_devs = dcr::OpenCLBackend::enumerate_devices();
    if (!ocl_devs.empty()) {
        for (size_t i = 0; i < ocl_devs.size(); ++i) {
            const auto& d = ocl_devs[i];
            std::cout << "   ├─ OpenCL #" << d.global_index << ": " << dcr::term::BRIGHT_WHITE << dcr::term::BOLD << d.device_name << dcr::term::RESET << "\n"
                      << "   │  ├─ Platform:           " << d.platform_name << "\n"
                      << "   │  ├─ Compute Units:      " << d.compute_units << " CUs\n"
                      << "   │  └─ Global Memory:      " << (d.global_mem_bytes / (1024 * 1024)) << " MB\n";
        }
    } else {
        std::cout << "   └─ " << dcr::term::DIM << "No OpenCL devices detected." << dcr::term::RESET << "\n";
    }
#endif

    std::cout << "\n" << dcr::term::BRIGHT_CYAN
              << "════════════════════════════════════════════════════════════════════════════════════════════\n"
              << dcr::term::RESET;
}

void print_stats_loop(dcr::Miner& miner) {
    while (g_running.load()) {
        for (int i = 0; i < 20; ++i) { // 2 seconds in 100ms increments
            if (!g_running.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!g_running.load()) break;

        miner.update_telemetry();
        miner.get_stats().update();
        miner.get_stats().print_dashboard_line();
    }
}

int main(int argc, char** argv) {
    dcr::term::init_console();
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    bool is_autotune = false;
    std::string mode = "solo";
    int cpu_threads = -1; // -1 = auto, 0 = disabled
    bool cuda_enabled = true;
    bool opencl_enabled = true;
    std::string rpc_host = "127.0.0.1:9109";
    std::string rpc_user = "user";
    std::string rpc_pass = "pass";
    std::string rpc_cert = "";
    std::string pool_url = "stratum+tcp://127.0.0.1:5550";
    std::string pool_user = "DsExampleAddress.worker1";
    std::string pool_pass = "x";
    std::string pool_failover = "";

    // Check environment variables
    if (const char* env_user = std::getenv("DCR_RPC_USER")) rpc_user = env_user;
    if (const char* env_pass = std::getenv("DCR_RPC_PASS")) rpc_pass = env_pass;
    if (const char* env_pool_user = std::getenv("DCR_POOL_USER")) pool_user = env_pool_user;
    if (const char* env_pool_pass = std::getenv("DCR_POOL_PASS")) pool_pass = env_pool_pass;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list-devices") {
            list_devices();
            return 0;
        } else if (arg == "--autotune") {
            is_autotune = true;
        } else if (arg == "--mode" && i + 1 < argc) {
            mode = argv[++i];
        } else if (arg == "--cpu" && i + 1 < argc) {
            std::string val = argv[++i];
            cpu_threads = (val == "auto") ? -1 : std::atoi(val.c_str());
        } else if (arg == "--no-cpu") {
            cpu_threads = 0;
        } else if (arg == "--cuda" && i + 1 < argc) {
            std::string val = argv[++i];
            cuda_enabled = (val != "0" && val != "none" && val != "off");
        } else if (arg == "--opencl" && i + 1 < argc) {
            std::string val = argv[++i];
            opencl_enabled = (val != "0" && val != "none" && val != "off");
        } else if (arg == "--rpc-host" && i + 1 < argc) {
            rpc_host = argv[++i];
        } else if (arg == "--rpc-user" && i + 1 < argc) {
            rpc_user = argv[++i];
        } else if (arg == "--rpc-pass" && i + 1 < argc) {
            rpc_pass = argv[++i];
        } else if (arg == "--rpc-cert" && i + 1 < argc) {
            rpc_cert = argv[++i];
        } else if (arg == "--pool" && i + 1 < argc) {
            pool_url = argv[++i];
        } else if (arg == "--user" && i + 1 < argc) {
            pool_user = argv[++i];
        } else if (arg == "--pass" && i + 1 < argc) {
            pool_pass = argv[++i];
        } else if (arg == "--pool-failover" && i + 1 < argc) {
            pool_failover = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            dcr::term::print_banner();
            std::cout << dcr::term::BOLD << "Usage: dcrminer [options]\n\n" << dcr::term::RESET
                      << dcr::term::BRIGHT_CYAN << "Device & Hardware Options:\n" << dcr::term::RESET
                      << "  --list-devices            List detected CPU & GPU hardware\n"
                      << "  --cpu <N|auto|0>          CPU worker thread count (default: auto)\n"
                      << "  --cuda <all|0|none>       Enable/disable NVIDIA CUDA\n"
                      << "  --opencl <all|0|none>     Enable/disable OpenCL (AMD / Intel GPUs)\n"
                      << "  --autotune                Run automated hardware tuner\n\n"
                      << dcr::term::BRIGHT_GREEN << "Mining & Network Options:\n" << dcr::term::RESET
                      << "  --mode <solo|stratum>     Mining mode (default: solo)\n"
                      << "  --rpc-host <host:port>    dcrd RPC host (default: 127.0.0.1:9109)\n"
                      << "  --rpc-user <user>         dcrd RPC username\n"
                      << "  --rpc-pass <pass>         dcrd RPC password\n"
                      << "  --rpc-cert <path>         Path to dcrd rpc.cert\n"
                      << "  --pool <stratum+tcp://...> Pool Stratum URL\n"
                      << "  --user <wallet.worker>    Pool username / wallet address\n"
                      << "  --pass <password>         Pool password\n"
                      << "  --pool-failover <url>     Backup failover pool URL\n\n";
            return 0;
        }
    }

    dcr::term::print_banner();

    if (is_autotune) {
        tuning::Autotuner tuner;
        tuner.run_full_scan();
        return 0;
    }

    dcr::Miner miner;
    miner.set_cpu_threads(cpu_threads);

#if defined(DCR_ENABLE_CUDA)
    if (cuda_enabled) {
        int cuda_count = 0;
        if (cudaGetDeviceCount(&cuda_count) == cudaSuccess && cuda_count > 0) {
            std::vector<int> devs;
            for (int d = 0; d < cuda_count; ++d) devs.push_back(d);
            miner.enable_cuda(devs);
            dcr::term::log_success("⚡ [CUDA]", "Initialized " + std::to_string(cuda_count) + " NVIDIA CUDA device(s).");
        }
    }
#endif

#if defined(DCR_ENABLE_OPENCL)
    if (opencl_enabled) {
        auto ocl_devs = dcr::OpenCLBackend::enumerate_devices();
        if (!ocl_devs.empty()) {
            std::vector<int> devs;
            for (const auto& d : ocl_devs) {
                if (cuda_enabled && d.platform_name.find("NVIDIA") != std::string::npos) {
                    continue; // Skip NVIDIA under OpenCL if CUDA is active
                }
                devs.push_back(d.global_index);
            }
            if (!devs.empty()) {
                miner.enable_opencl(devs);
                dcr::term::log_success("🔥 [OPENCL]", "Initialized " + std::to_string(devs.size()) + " OpenCL device(s) (AMD/Intel).");
            }
        }
    }
#endif

    dcr::DevFeeManager devfee_manager(miner);
    miner.set_solution_callback([&devfee_manager](const dcr::Solution& sol, const dcr::MiningJob& job) {
        devfee_manager.handle_solution(sol, job);
    });

    std::unique_ptr<decred::RpcClient> rpc_client;
    std::unique_ptr<decred::StratumClient> stratum_client;

    if (mode == "solo") {
        dcr::term::log_info("🚀 [MODE]", "Solo mining via dcrd RPC (" + rpc_host + ")");
        rpc_client = std::make_unique<decred::RpcClient>(rpc_host, rpc_user, rpc_pass, rpc_cert);
        rpc_client->set_work_callback([&devfee_manager](auto job) {
            devfee_manager.handle_user_job(job);
        });
        devfee_manager.set_user_solution_callback([&rpc_client](const dcr::Solution& sol, const dcr::MiningJob& job) {
            std::cout << "\n" << dcr::term::BRIGHT_GREEN << dcr::term::BOLD
                      << "╔══════════════════════════════════════════════════════════════════════════════════════════╗\n"
                      << "║  🎉 [SOLVER] VALID POW BLOCK / SHARE FOUND!                                             ║\n"
                      << "╠══════════════════════════════════════════════════════════════════════════════════════════╣\n"
                      << "║  🔑 Nonce:    " << dcr::term::BRIGHT_YELLOW << "0x" << std::hex << sol.nonce << std::dec
                      << " (" << sol.nonce << ")\n"
                      << "║  ⚡ Hash:     " << dcr::term::BRIGHT_CYAN << dcr::hex_encode(sol.pow_hash.data(), 32) << "\n"
                      << "║  📡 Device:   #" << sol.device_id << "\n"
                      << "╚══════════════════════════════════════════════════════════════════════════════════════════╝\n"
                      << dcr::term::RESET << "\n";
            dcr::HeaderBytes solved_header = job.header_template;
            if (!sol.extra_nonce2_hex.empty()) {
                uint32_t en2_val = sol.extra_nonce2;
                solved_header[148] = static_cast<uint8_t>((en2_val >> 24) & 0xFF);
                solved_header[149] = static_cast<uint8_t>((en2_val >> 16) & 0xFF);
                solved_header[150] = static_cast<uint8_t>((en2_val >> 8) & 0xFF);
                solved_header[151] = static_cast<uint8_t>(en2_val & 0xFF);
            }
            dcr::DcrHeader::set_nonce(solved_header, static_cast<uint32_t>(sol.nonce));
            std::string hex = dcr::hex_encode(solved_header.data(), 180);
            rpc_client->submit_work(hex);
        });
        rpc_client->start();
    } else {
        dcr::term::log_info("🌐 [MODE]", "Pool mining via Stratum (" + pool_url + ")");
        stratum_client = std::make_unique<decred::StratumClient>(pool_url, pool_user, pool_pass, pool_failover);
        stratum_client->set_work_callback([&devfee_manager](auto job) {
            devfee_manager.handle_user_job(job);
        });
        devfee_manager.set_user_solution_callback([&stratum_client](const dcr::Solution& sol, const dcr::MiningJob& job) {
            std::cout << "\n" << dcr::term::BRIGHT_GREEN << dcr::term::BOLD
                      << "╔══════════════════════════════════════════════════════════════════════════════════════════╗\n"
                      << "║  💎 [SHARE FOUND] VALID POW SHARE SOLVED!                                               ║\n"
                      << "╠══════════════════════════════════════════════════════════════════════════════════════════╣\n"
                      << "║  🔑 Nonce:    " << dcr::term::BRIGHT_YELLOW << "0x" << std::hex << sol.nonce << std::dec
                      << " (" << sol.nonce << ")\n"
                      << "║  ⚡ Hash:     " << dcr::term::BRIGHT_CYAN << dcr::hex_encode(sol.pow_hash.data(), 32) << "\n"
                      << "║  📡 Device:   #" << sol.device_id << "\n"
                      << "╚══════════════════════════════════════════════════════════════════════════════════════════╝\n"
                      << dcr::term::RESET << "\n";
            std::string job_id_str = job.stratum_job_id.empty() ? std::to_string(job.job_id) : job.stratum_job_id;
            std::string en2 = sol.extra_nonce2_hex.empty() ? (job.extra_nonce2_hex.empty() ? "00000000" : job.extra_nonce2_hex) : sol.extra_nonce2_hex;
            stratum_client->submit_work(job_id_str, en2, "", std::to_string(sol.nonce));
        });
        stratum_client->start();
    }

    devfee_manager.start();

    dcr::term::log_info("⛏️  [MINER]", "Starting high-performance mining engine...");
    miner.start();

    // Stats loop
    print_stats_loop(miner);

    dcr::term::log_info("🛑 [MINER]", "Stopping worker threads...");
    miner.stop();
    devfee_manager.stop();
    if (rpc_client) rpc_client->stop();
    if (stratum_client) stratum_client->stop();

    dcr::term::log_success("✨ [MINER]", "Clean shutdown complete. Happy mining!");
    return 0;
}
