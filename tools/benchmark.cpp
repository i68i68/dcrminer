#include "../src/common/types.hpp"
#include "../src/blake3/blake3_ref.hpp"
#include "../src/blake3/blake3_constants.hpp"
#include "../src/blake3/cpu_features.hpp"
#include "../src/blake3/dcr_blake3_scalar.hpp"
#include "../src/blake3/dcr_blake3_dispatch.hpp"

#if defined(DCR_ENABLE_CUDA)
#include "../src/gpu/cuda/cuda_backend.hpp"
#endif

#include "../src/common/terminal.hpp"

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <iomanip>
#include <atomic>

using namespace dcr;

// Benchmark realistic header template
static HeaderBytes get_benchmark_header() {
    HeaderBytes header{};
    for (size_t i = 0; i < 180; ++i) {
        header[i] = static_cast<uint8_t>((i * 37 + 11) & 0xFF);
    }
    write_le32(header.data(), 1); // version
    write_le32(header.data() + 116, 0x1d00ffff); // bits
    write_le32(header.data() + 128, 800000); // height
    write_le32(header.data() + 132, 180); // size
    write_le32(header.data() + 136, 1727400000); // timestamp
    write_le32(header.data() + 176, 11); // stake_version
    return header;
}

volatile uint32_t g_sink = 0;

void benchmark_scalar(int duration_sec) {
    std::cout << "  [CPU] Scalar (1 thread) ... " << std::flush;
    HeaderBytes header = get_benchmark_header();
    ChainValue midstate = blake3::compute_midstate(header);
    MsgBlock block2 = blake3::prepare_block2_words(header);

    // Warmup
    uint32_t out[8];
    for (uint32_t i = 0; i < 50000; ++i) {
        blake3::scalar_hash_final_block(midstate.data(), block2.data(), i, out);
    }

    auto start = std::chrono::steady_clock::now();
    auto end_target = start + std::chrono::seconds(duration_sec);
    uint64_t hashes = 0;
    uint32_t nonce = 100000;
    uint32_t acc = 0;

    while (std::chrono::steady_clock::now() < end_target) {
        for (int b = 0; b < 1000; ++b) {
            blake3::scalar_hash_final_block(midstate.data(), block2.data(), nonce++, out);
            acc ^= out[0] ^ out[7];
        }
        hashes += 1000;
    }
    g_sink = acc;

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    double hr = static_cast<double>(hashes) / elapsed;

    std::cout << std::fixed << std::setprecision(2) << (hr / 1e6) << " MH/s ("
              << hashes << " hashes in " << elapsed << " s)" << std::endl;
}

void benchmark_avx2_single(int duration_sec) {
    std::cout << "  [CPU] AVX2 (1 thread, 8 lanes) ... " << std::flush;
    HeaderBytes header = get_benchmark_header();
    ChainValue midstate = blake3::compute_midstate(header);
    MsgBlock block2 = blake3::prepare_block2_words(header);

    alignas(32) uint32_t out[8][8];
    // Warmup
    for (uint32_t i = 0; i < 50000; i += 8) {
        blake3::avx2_hash_batch_dispatch(midstate.data(), block2.data(), i, out);
    }

    auto start = std::chrono::steady_clock::now();
    auto end_target = start + std::chrono::seconds(duration_sec);
    uint64_t hashes = 0;
    uint32_t nonce = 100000;
    uint32_t acc = 0;

    while (std::chrono::steady_clock::now() < end_target) {
        for (int b = 0; b < 250; ++b) {
            blake3::avx2_hash_batch_dispatch(midstate.data(), block2.data(), nonce, out);
            acc ^= out[0][0] ^ out[7][7];
            nonce += 8;
        }
        hashes += 2000; // 250 * 8
    }
    g_sink = acc;

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    double hr = static_cast<double>(hashes) / elapsed;

    std::cout << std::fixed << std::setprecision(2) << (hr / 1e6) << " MH/s ("
              << hashes << " hashes in " << elapsed << " s)" << std::endl;
}

void benchmark_avx2_multithread(int duration_sec, int num_threads) {
    std::cout << "  [CPU] AVX2 (" << num_threads << " threads) ... " << std::flush;
    HeaderBytes header = get_benchmark_header();
    ChainValue midstate = blake3::compute_midstate(header);
    MsgBlock block2 = blake3::prepare_block2_words(header);

    std::atomic<bool> running{true};
    std::atomic<uint64_t> total_hashes{0};

    std::vector<std::thread> workers;
    for (int t = 0; t < num_threads; ++t) {
        workers.emplace_back([&, t]() {
            alignas(32) uint32_t out[8][8];
            uint32_t nonce = static_cast<uint32_t>(t * 100000000ULL);
            uint64_t local_hashes = 0;
            uint32_t acc = 0;

            while (running.load(std::memory_order_relaxed)) {
                for (int b = 0; b < 250; ++b) {
                    blake3::avx2_hash_batch_dispatch(midstate.data(), block2.data(), nonce, out);
                    acc ^= out[0][0] ^ out[7][7];
                    nonce += 8;
                }
                local_hashes += 2000;
            }
            total_hashes.fetch_add(local_hashes, std::memory_order_relaxed);
            g_sink ^= acc;
        });
    }

    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(duration_sec));
    running.store(false);

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    uint64_t hashes = total_hashes.load();
    double hr = static_cast<double>(hashes) / elapsed;

    std::cout << std::fixed << std::setprecision(2) << (hr / 1e6) << " MH/s ("
              << hashes << " hashes in " << elapsed << " s)" << std::endl;
}

void benchmark_avx512_single(int duration_sec) {
    std::cout << "  [CPU] AVX-512 (1 thread, 16 lanes) ... " << std::flush;
    HeaderBytes header = get_benchmark_header();
    ChainValue midstate = blake3::compute_midstate(header);
    MsgBlock block2 = blake3::prepare_block2_words(header);

    alignas(64) uint32_t out[8][16];
    // Warmup
    for (uint32_t i = 0; i < 50000; i += 16) {
        blake3::avx512_hash_batch_dispatch(midstate.data(), block2.data(), i, out);
    }

    auto start = std::chrono::steady_clock::now();
    auto end_target = start + std::chrono::seconds(duration_sec);
    uint64_t hashes = 0;
    uint32_t nonce = 100000;
    uint32_t acc = 0;

    while (std::chrono::steady_clock::now() < end_target) {
        for (int b = 0; b < 250; ++b) {
            blake3::avx512_hash_batch_dispatch(midstate.data(), block2.data(), nonce, out);
            acc ^= out[0][0] ^ out[7][15];
            nonce += 16;
        }
        hashes += 4000; // 250 * 16
    }
    g_sink = acc;

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    double hr = static_cast<double>(hashes) / elapsed;

    std::cout << std::fixed << std::setprecision(2) << (hr / 1e6) << " MH/s ("
              << hashes << " hashes in " << elapsed << " s)" << std::endl;
}

void benchmark_avx512_multithread(int duration_sec, int num_threads) {
    std::cout << "  [CPU] AVX-512 (" << num_threads << " threads) ... " << std::flush;
    HeaderBytes header = get_benchmark_header();
    ChainValue midstate = blake3::compute_midstate(header);
    MsgBlock block2 = blake3::prepare_block2_words(header);

    std::atomic<bool> running{true};
    std::atomic<uint64_t> total_hashes{0};

    std::vector<std::thread> workers;
    for (int t = 0; t < num_threads; ++t) {
        workers.emplace_back([&, t]() {
            alignas(64) uint32_t out[8][16];
            uint32_t nonce = static_cast<uint32_t>(t * 100000000ULL);
            uint64_t local_hashes = 0;
            uint32_t acc = 0;

            while (running.load(std::memory_order_relaxed)) {
                for (int b = 0; b < 250; ++b) {
                    blake3::avx512_hash_batch_dispatch(midstate.data(), block2.data(), nonce, out);
                    acc ^= out[0][0] ^ out[7][15];
                    nonce += 16;
                }
                local_hashes += 4000;
            }
            total_hashes.fetch_add(local_hashes, std::memory_order_relaxed);
            g_sink ^= acc;
        });
    }

    auto start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::seconds(duration_sec));
    running.store(false);

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    uint64_t hashes = total_hashes.load();
    double hr = static_cast<double>(hashes) / elapsed;

    std::cout << std::fixed << std::setprecision(2) << (hr / 1e6) << " MH/s ("
              << hashes << " hashes in " << elapsed << " s)" << std::endl;
}

#if defined(DCR_ENABLE_CUDA)
void benchmark_cuda(int device_id, int duration_sec) {
    std::cout << "  [GPU] CUDA (Device " << device_id << ") ... " << std::flush;
    HeaderBytes header = get_benchmark_header();
    ChainValue midstate = blake3::compute_midstate(header);
    MsgBlock block2 = blake3::prepare_block2_words(header);
    Target256 impossible_target{}; // All zeros -> only benchmarking speed

    CudaBackend cuda;
    if (!cuda.init(device_id)) {
        std::cout << "FAILED to init device " << device_id << std::endl;
        return;
    }
    std::cout << "(" << cuda.device_name() << ") ... " << std::flush;
    cuda.set_job(midstate, block2, impossible_target);

    // Warmup
    constexpr uint32_t BATCH = 4194304; // 4M nonces per launch
    cuda.mine(0, BATCH);

    auto start = std::chrono::steady_clock::now();
    auto end_target = start + std::chrono::seconds(duration_sec);
    uint64_t hashes = 0;
    uint32_t nonce = 1000000;

    while (std::chrono::steady_clock::now() < end_target) {
        hashes += cuda.mine(nonce, BATCH);
        nonce += BATCH;
    }

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    double hr = static_cast<double>(hashes) / elapsed;

    std::cout << std::fixed << std::setprecision(2) << (hr / 1e9) << " GH/s ("
              << (hr / 1e6) << " MH/s, " << hashes << " hashes in " << elapsed << " s)" << std::endl;

    cuda.shutdown();
}
#endif

#if defined(DCR_ENABLE_OPENCL)
#include "../src/gpu/opencl/opencl_backend.hpp"
void benchmark_opencl(int device_id, int duration_sec) {
    auto ocl_devs = OpenCLBackend::enumerate_devices();
    if (ocl_devs.empty() || device_id >= static_cast<int>(ocl_devs.size())) {
        std::cout << "  [GPU] OpenCL: No OpenCL device with ID " << device_id << " available.\n";
        return;
    }
    std::cout << "  [GPU] OpenCL (Device " << device_id << ") ... " << std::flush;
    HeaderBytes header = get_benchmark_header();
    ChainValue midstate = blake3::compute_midstate(header);
    MsgBlock block2 = blake3::prepare_block2_words(header);
    Target256 impossible_target{};

    OpenCLBackend ocl;
    if (!ocl.init(device_id)) {
        std::cout << "FAILED to init OpenCL device " << device_id << std::endl;
        return;
    }
    std::cout << "(" << ocl.device_name() << ") ... " << std::flush;
    ocl.set_job(midstate, block2, impossible_target);

    constexpr uint32_t BATCH = 4194304;
    ocl.mine(0, BATCH); // Warmup

    auto start = std::chrono::steady_clock::now();
    auto end_target = start + std::chrono::seconds(duration_sec);
    uint64_t hashes = 0;
    uint32_t nonce = 1000000;

    while (std::chrono::steady_clock::now() < end_target) {
        hashes += ocl.mine(nonce, BATCH);
        nonce += BATCH;
    }

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    double hr = static_cast<double>(hashes) / elapsed;

    std::cout << std::fixed << std::setprecision(2) << (hr / 1e9) << " GH/s ("
              << (hr / 1e6) << " MH/s, " << hashes << " hashes in " << elapsed << " s)" << std::endl;

    ocl.shutdown();
}
#endif

int main(int argc, char** argv) {
    dcr::term::init_console();
    int duration = 3;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--duration" && i + 1 < argc) {
            duration = std::atoi(argv[++i]);
        }
    }

    dcr::term::print_banner();

    std::cout << dcr::term::BRIGHT_CYAN << dcr::term::BOLD
              << "╔══════════════════════════════════════════════════════════════════════════════════════════╗\n"
              << "║                   🏁 HARDWARE BENCHMARK & PERFORMANCE AUDIT SUITE                        ║\n"
              << "╚══════════════════════════════════════════════════════════════════════════════════════════╝\n"
              << dcr::term::RESET << "\n";

    auto features = blake3::detect_cpu_features();
    std::string cpu_brand = blake3::get_cpu_brand_string();
    std::cout << dcr::term::BRIGHT_YELLOW << dcr::term::BOLD << "💻 [CPU SIMD Features: " << cpu_brand << "]:" << dcr::term::RESET << "\n";
    std::cout << "   ├─ Processor: " << cpu_brand << "\n";
    std::cout << "   ├─ SSE4.1:    " << (features.sse41 ? (dcr::term::BRIGHT_GREEN + std::string("✔ YES")) : (dcr::term::DIM + std::string("✖ NO"))) << dcr::term::RESET << "\n";
    std::cout << "   ├─ AVX2:      " << (features.avx2 ? (dcr::term::BRIGHT_GREEN + std::string("✔ YES (8 Lanes parallel)")) : (dcr::term::DIM + std::string("✖ NO"))) << dcr::term::RESET << "\n";
    std::cout << "   └─ AVX-512:   " << (features.avx512f ? (dcr::term::BRIGHT_GREEN + std::string("✔ YES (16 Lanes parallel)")) : (dcr::term::DIM + std::string("✖ NO"))) << dcr::term::RESET << "\n\n";

    std::cout << dcr::term::BRIGHT_WHITE << dcr::term::BOLD
              << "⏱️  Running Hardware Benchmarks (" << duration << "s per test):\n"
              << "──────────────────────────────────────────────────────────────────────────────────────────\n"
              << dcr::term::RESET;

    benchmark_scalar(duration);

    if (features.avx2 && blake3::has_avx2_runtime()) {
        benchmark_avx2_single(duration);
        int cores = static_cast<int>(std::thread::hardware_concurrency());
        if (cores > 1) {
            benchmark_avx2_multithread(duration, cores);
        }
    }

    if (features.avx512f && blake3::has_avx512_runtime()) {
        benchmark_avx512_single(duration);
        int cores = static_cast<int>(std::thread::hardware_concurrency());
        if (cores > 1) {
            benchmark_avx512_multithread(duration, cores);
        }
    }

#if defined(DCR_ENABLE_CUDA)
    std::cout << "\n" << dcr::term::BRIGHT_GREEN << dcr::term::BOLD << "🟢 [GPU Benchmarks (Native CUDA)]:" << dcr::term::RESET << "\n";
    std::cout << "──────────────────────────────────────────────────────────────────────────────────────────\n";
    benchmark_cuda(0, duration);
#endif

#if defined(DCR_ENABLE_OPENCL)
    std::cout << "\n" << dcr::term::BRIGHT_MAGENTA << dcr::term::BOLD << "🔴 [GPU Benchmarks (OpenCL - AMD / Intel / NVIDIA)]:" << dcr::term::RESET << "\n";
    std::cout << "──────────────────────────────────────────────────────────────────────────────────────────\n";
    auto ocl_devs = OpenCLBackend::enumerate_devices();
    for (size_t i = 0; i < ocl_devs.size(); ++i) {
        benchmark_opencl(static_cast<int>(i), duration);
    }
#endif

    std::cout << "\n" << dcr::term::BRIGHT_CYAN
              << "========================================================================================\n"
              << "  ✨ Benchmark Complete! Real hardware hashrates verified.\n"
              << "========================================================================================\n"
              << dcr::term::RESET << "\n";

    return 0;
}
