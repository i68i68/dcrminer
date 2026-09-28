<div align="center">

# ⚡ dcrminer

### Ultra-Optimized Decred (DCR) BLAKE3 Proof-of-Work Miner (DCP-0011)

[![GitHub Release](https://img.shields.io/github/v/release/i68i68/dcrminer?style=for-the-badge&color=0072ce)](https://github.com/i68i68/dcrminer/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-2ea44f.svg?style=for-the-badge)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-blue?style=for-the-badge&logo=linux&logoColor=white)](https://github.com/i68i68/dcrminer)
[![C++20](https://img.shields.io/badge/Standard-C%2B%2B20-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![CUDA](https://img.shields.io/badge/NVIDIA-CUDA%2012%2B-76B900?style=for-the-badge&logo=nvidia&logoColor=white)](https://developer.nvidia.com/cuda-toolkit)
[![OpenCL](https://img.shields.io/badge/Compute-OpenCL%202.0%2B-green?style=for-the-badge&logo=khronos&logoColor=white)](https://www.khronos.org/opencl/)

**A high-performance, production-grade miner engineered from the ground up for Decred's DCP-0011 BLAKE3 consensus upgrade.**  
Supports parallel CPU SIMD acceleration (AVX-512, AVX2) and multi-vendor GPU execution (NVIDIA CUDA, AMD/Intel OpenCL).

[Features](#-key-features) • [Hashrates](#-benchmark-performance) • [Quickstart](#-quickstart--installation) • [Mining Guide](#-mining-operations) • [CLI Options](#-command-line-reference) • [Dev Fee](#-developer-fee-01)

</div>

---

## 🌟 Key Features

- **Consensus-Specific DCP-0011 Engine**:
  Decred's 180-byte header spans 3 BLAKE3 compression blocks. `dcrminer` precomputes the midstate chaining value ($CV_2$) after blocks 0 and 1 on the host, executing **only the final 52-byte block** inside the inner nonce search loop.
- **Zero-Word Arithmetic Elimination**:
  In Decred's final block layout, message words $m[13], m[14], m[15]$ are strictly zero padding. All additions and quarter-round mixing steps involving these words are omitted at compile time for maximum throughput.
- **Dynamic Multi-CPU Vector Dispatch**:
  - **AVX-512 (16 Lanes Parallel)**: Uses native `_mm512_ror_epi32` instructions with zero register spill.
  - **AVX2 (8 Lanes Parallel)**: Utilizes `_mm256_shuffle_epi8` for instantaneous 8-bit and 16-bit word rotations.
  - **Scalar Fallback**: Clean baseline implementation guaranteeing safe execution on older x86_64 machines.
  - **Dynamic CPUID Detection**: Detects CPU instruction sets at runtime; will **never crash** with `Illegal Instruction` on non-AVX processors.
- **Multi-Vendor GPU Acceleration**:
  - **NVIDIA CUDA**: Fully unrolled 7-round compression kernel kept 100% in hardware registers. Constant memory caching for block headers and fast `__byte_perm` rotations. Multi-architecture binary targets Maxwell (SM 5.0) through Blackwell (SM 10.0+).
  - **AMD & Intel OpenCL**: Cross-platform compute kernels with dynamic workgroup sizing to prevent `CL_INVALID_WORK_GROUP_SIZE (-54)` errors on integrated and discrete GPUs.
- **Lock-Free Asynchronous Orchestration**:
  High-throughput atomic nonce partitioning and zero-allocation mining loops ensure CPU and GPU workers run at full throttle without lock contention.
- **Industrial Networking**:
  Robust Stratum JSON-RPC client and dcrd JSON-RPC `getwork` solo client with exponential backoff reconnects and automatic pool failover.

---

## 📊 Benchmark Performance

Empirical benchmarks measured on reference host hardware running genuine 180-byte Decred DCP-0011 block headers:

| Hardware Device | Mining Engine | Threads / Lanes | Hashrate |
| :--- | :--- | :--- | :--- |
| **Intel CPU** | AVX-512 SIMD | 64 Threads (16 lanes/thread) | **3,396.03 MH/s (3.40 GH/s)** |
| **Intel CPU** | AVX2 SIMD | 64 Threads (8 lanes/thread) | **1,901.04 MH/s (1.90 GH/s)** |
| **Intel CPU** | AVX-512 SIMD | 1 Thread (Single Core) | **107.46 MH/s** |
| **Intel CPU** | AVX2 SIMD | 1 Thread (Single Core) | **58.89 MH/s** |
| **Intel CPU** | Scalar Baseline | 1 Thread | **8.60 MH/s** |
| **NVIDIA Quadro P2200** | Native CUDA | Maxwell/Pascal Architecture | **2,611.16 MH/s (2.61 GH/s)** |
| **NVIDIA RTX 4090 (Est.)** | Native CUDA | Ada Lovelace Architecture | **~24.50 GH/s** |
| **Dual System Combined** | **CPU (AVX-512) + GPU** | **Full System Concurrent** | **6,007.19 MH/s (6.01 GH/s)** |

---

## 🚀 Quickstart & Installation

Pre-compiled release binaries are available under [Releases](https://github.com/i68i68/dcrminer/releases).

### Building from Source

#### 1. Windows (MSVC 2022 + Ninja or Visual Studio)
Run the 1-click build script:
```cmd
build_windows.bat
```
Or build manually from an **x64 Native Tools Command Prompt for VS 2022**:
```cmd
cmake -B build_win -G Ninja -DCMAKE_BUILD_TYPE=Release -DDCR_ENABLE_CUDA=ON -DDCR_ENABLE_OPENCL=ON
ninja -C build_win
```

#### 2. Linux (Ubuntu / Debian / Fedora / Arch)
Install dependencies:
```bash
# Ubuntu / Debian
sudo apt update && sudo apt install -y build-essential cmake ninja-build ocl-icd-opencl-dev

# Optional: NVIDIA CUDA Toolkit (if using NVIDIA GPUs)
# sudo apt install -y nvidia-cuda-toolkit
```
Run the 1-click build script:
```bash
chmod +x build_linux.sh
./build_linux.sh
```

---

## ⛏️ Mining Operations

### 1. Stratum Pool Mining (Recommended)
Connect to any standard Decred mining pool:

```bash
# Mine with all CPU cores and all detected GPUs concurrently:
dcrminer --mode stratum --pool stratum+tcp://dcr.poolpy.lol:5550 --user DsYourWalletAddress.worker1 --pass x --cpu auto --cuda all

# Mine with GPU only (0% CPU utilization):
dcrminer --mode stratum --pool stratum+tcp://dcr.poolpy.lol:5550 --user DsYourWalletAddress.worker1 --pass x --cpu 0 --cuda all

# Mine with CPU only:
dcrminer --mode stratum --pool stratum+tcp://dcr.poolpy.lol:5550 --user DsYourWalletAddress.worker1 --pass x --cpu auto --cuda none
```

### 2. Automatic Failover Pool
Ensure 24/7 uptime by specifying a fallback stratum pool:
```bash
dcrminer --mode stratum --pool stratum+tcp://primary-pool.com:5550 --pool-failover stratum+tcp://backup-pool.com:5550 --user DsYourWalletAddress.worker1 --pass x --cpu auto --cuda all
```

### 3. Solo Mining (Direct dcrd RPC)
Mine directly against your local or remote Decred full node:
```bash
dcrminer --mode solo --rpc-host 127.0.0.1:9109 --rpc-user your_rpc_user --rpc-pass your_rpc_password --cpu auto --cuda all
```

### 4. Running the Built-in Hardware Benchmark
Evaluate your system's exact hashrate across all SIMD backends:
```bash
# Run 10-second accurate hardware benchmark
dcr_benchmark --duration 10
```

---

## ⚙️ Command-Line Reference

| Argument | Values / Default | Description |
| :--- | :--- | :--- |
| `--list-devices` | - | Probe and display detected CPU extensions and GPU compute devices. |
| `--mode` | `stratum` \| `solo` | Mining protocol mode (default: `stratum`). |
| `--pool` | `stratum+tcp://host:port` | Primary Stratum pool URL. |
| `--pool-failover`| `stratum+tcp://host:port` | Backup Stratum pool URL for seamless failover. |
| `--user` | `string` | Worker username or wallet address. |
| `--pass` | `string` | Worker password (default: `x`). |
| `--cpu` | `auto` \| `N` \| `0` | Number of CPU mining threads (`auto` = all logical cores). |
| `--cuda` | `all` \| `N` \| `none` | NVIDIA CUDA GPU selector (`all`, device index, or `none`). |
| `--opencl` | `all` \| `N` \| `none` | OpenCL GPU selector for AMD Radeon & Intel GPUs. |
| `--rpc-host` | `ip:port` | dcrd RPC server endpoint for solo mining. |
| `--rpc-user` | `string` | dcrd RPC username. |
| `--rpc-pass` | `string` | dcrd RPC password. |
| `--rpc-cert` | `path` | Path to `rpc.cert` for TLS encrypted RPC connections. |

Credentials can also be provided securely via environment variables:
- `DCR_POOL_USER`, `DCR_POOL_PASS`
- `DCR_RPC_USER`, `DCR_RPC_PASS`

---

## 💡 Developer Fee (0.1%)

`dcrminer` includes a modest **0.1% developer fee** (3.6 seconds out of every 1-hour mining period) to fund continued maintenance, optimizations, and support for future hardware generations.

- **10-Minute Warmup Grace Period**: The miner operates 100% on the user's job for the first 10 minutes after launch before any fee cycle occurs.
- **Fail-Safe Fallback**: If the developer pool is unreachable, encounters a network timeout, or drops connection, the miner **instantly aborts** the fee cycle and resumes 100% mining on the user's pool. Your hardware will never stall or waste cycles.
- **Strict Cryptographic Isolation**: Fee shares and user shares use immutable job tags to eliminate any risk of share misattribution.

---

## 📁 Repository Layout

```
dcrminer/
├── CMakeLists.txt          # Universal build configuration (C++20, CUDA, OpenCL)
├── build_windows.bat       # 1-Click Windows MSVC build script
├── build_linux.sh          # 1-Click Linux build script
├── README.md               # Project documentation
├── COMMANDS.md             # Complete CLI reference and command examples
├── PERFORMANCE.md          # Detailed benchmark report
├── configs/
│   └── example.toml        # Sample TOML configuration file
├── src/
│   ├── blake3/             # BLAKE3 implementation & CPU SIMD engines
│   │   ├── cpu_features.hpp        # Runtime CPUID feature detection
│   │   ├── dcr_blake3_dispatch.hpp # Dynamic SIMD dispatcher
│   │   ├── dcr_blake3_scalar.hpp   # Clean scalar baseline
│   │   ├── dcr_blake3_avx2.cpp     # 8-lane AVX2 SIMD kernel
│   │   └── dcr_blake3_avx512.cpp   # 16-lane AVX-512 SIMD kernel
│   ├── core/               # Mining coordinator & thread workers
│   │   ├── devfee.cpp / .hpp       # Transparent fail-safe 0.1% fee manager
│   │   ├── job.hpp                 # Thread-safe immutable job representation
│   │   ├── miner.cpp / .hpp        # Device orchestration & work distribution
│   │   ├── nonce_allocator.hpp     # Collision-free atomic nonce dispenser
│   │   └── stats.hpp               # Lock-free performance telemetry
│   ├── decred/             # Consensus & network protocols
│   │   ├── header.hpp              # 180-byte header serialization
│   │   ├── pow.hpp                 # BLAKE3 PoW verification
│   │   ├── rpc_client.cpp / .hpp   # dcrd JSON-RPC client
│   │   └── stratum_client.cpp/.hpp # Stratum protocol implementation
│   ├── gpu/                # Hardware GPU accelerators
│   │   ├── cuda/                   # Native NVIDIA CUDA backend & kernels
│   │   ├── opencl/                 # Universal OpenCL backend (AMD/Intel)
│   │   └── hip/                    # AMD HIP source backend
│   └── main.cpp            # Production CLI application entry point
└── tools/
    └── benchmark.cpp       # Standalone hardware benchmarking utility
```

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).

Contributions, bug reports, and pull requests are welcome at [github.com/i68i68/dcrminer](https://github.com/i68i68/dcrminer).
