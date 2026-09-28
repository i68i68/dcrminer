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

[🚀 Quick Start (No Dev Tools Needed)](#-quick-start-for-miners-no-dev-tools-required) • [Hashrates](#-benchmark-performance) • [Features](#-key-features) • [Build from Source](#-building-from-source) • [Mining Guide](#-mining-operations) • [CLI Reference](#-command-line-reference) • [คู่มือภาษาไทย](#-คู่มือเริ่มต้นใช้งานฉบับภาษาไทย)

</div>

---

## 🚀 Quick Start for Miners (No Dev Tools Required!)

> [!TIP]
> **You do NOT need to install Visual Studio, C++, CMake, or compile anything!**  
> We provide pre-compiled, zero-dependency standalone binaries for Windows 10/11.

### 📥 1-Click Download:
1. Go to the **[Latest GitHub Releases](https://github.com/i68i68/dcrminer/releases)**.
2. Download **`dcrminer-v0.1.0-windows-x64.zip`**.
3. Extract the ZIP anywhere on your PC (e.g. Desktop).

### ⛏️ Start Mining in 10 Seconds:
1. Right-click on **`MINE_POOLPY.bat`** and click **Edit** (or open with Notepad).
2. Change the `WALLET` variable to your own Decred (DCR) address:
   ```cmd
   set WALLET=Dsc8zVfG93V2g1iC3tFj1mZ2LqY9bV3KxYx
   set WORKER=rig1
   ```
3. Save and double-click **`MINE_POOLPY.bat`** to start mining!

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
- **Zero-Dependency Portable Binary**:
  Windows binary is compiled with static runtime (`/MT`), requiring zero external Visual C++ DLLs or installers.

---

## 🛠️ Building from Source (For Developers)

If you wish to compile `dcrminer` from source code on your machine:

### 1. Windows Build (Visual Studio 2022)

#### Prerequisites:
- Visual Studio 2022 (Community, Build Tools, or Professional) with **Desktop development with C++**
- CMake (v3.20+)
- Ninja (Recommended for fastest compilation)
- *Optional:* NVIDIA CUDA Toolkit (v12.0+) if building with NVIDIA GPU support.

#### Automated Setup via Windows Package Manager (`winget`):
You can install all required build tools with one command in an Administrator PowerShell:
```powershell
winget install Microsoft.VisualStudio.2022.BuildTools Kitware.CMake Ninja-build.Ninja --override "--passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

#### Run the 1-Click Windows Builder:
Double-click **`build_windows.bat`** or run:
```cmd
build_windows.bat
```
> [!NOTE]
> `build_windows.bat` automatically locates MSVC, detects CUDA/Ninja, compiles the standalone static executable, and prints diagnostic troubleshooting if any tools are missing. It will never exit silently!

---

### 2. Linux Build (Ubuntu / Debian / Fedora / Arch)

#### Prerequisites:
```bash
# Ubuntu / Debian
sudo apt update && sudo apt install -y build-essential cmake ninja-build ocl-icd-opencl-dev

# Optional: NVIDIA CUDA Toolkit (if compiling with CUDA)
# sudo apt install -y nvidia-cuda-toolkit
```

#### Build:
```bash
chmod +x build_linux.sh
./build_linux.sh
```

---

## ⛏️ Mining Operations

### 1. Stratum Pool Mining (Recommended)
Connect to any standard Decred mining pool (such as [PoolPy.lol](https://poolpy.lol)):

```bash
# Mine on PoolPy.lol with auto-detected CPU threads
dcrminer.exe -o stratum+tcp://stratum.poolpy.lol:5550 -u Dsc8zVfG93V2g1iC3tFj1mZ2LqY9bV3KxYx.rig1 -p x

# Mine with both CPU and all NVIDIA GPUs
dcrminer.exe -o stratum+tcp://stratum.poolpy.lol:5550 -u Dsc8zVfG93V2g1iC3tFj1mZ2LqY9bV3KxYx.rig1 -p x --cpu auto --cuda all

# Mine with specific number of CPU threads (e.g. 16 threads)
dcrminer.exe -o stratum+tcp://stratum.poolpy.lol:5550 -u Dsc8zVfG93V2g1iC3tFj1mZ2LqY9bV3KxYx.rig1 -p x --cpu 16
```

### 2. Solo Mining (Direct to local dcrd node)
```bash
dcrminer.exe --mode solo --rpc-host 127.0.0.1:9109 --rpc-user your_user --rpc-pass your_pass
```

### 3. Hardware Benchmark
```bash
# Fast 10-second benchmark of all available backends
dcr_benchmark.exe

# Or via main miner
dcrminer.exe --benchmark
```

---

## 📋 Command-Line Reference

```
Usage: dcrminer [OPTIONS]

General Options:
  -h, --help               Display this help guide and exit
  -v, --version            Display version information
  --list-devices           Display all detected CPU capabilities, GPUs, and SIMD widths
  --benchmark              Execute a standalone multi-threaded benchmark

Connection Modes:
  --mode <solo|stratum>    Mining mode (Default: stratum)
  -o, --pool <url>         Stratum pool endpoint (e.g., stratum+tcp://stratum.poolpy.lol:5550)
  -u, --user <wallet>      Mining wallet address and optional .worker suffix
  -p, --pass <password>    Stratum authorization password (Default: x)

Worker Hardware Controls:
  --cpu <auto|N|off>       CPU worker allocation (auto = physical cores, N = thread count, off = disable)
  --cuda <all|N|off>       NVIDIA CUDA devices (all = all GPUs, N = specific device index, off = disable)
  --opencl <all|N|off>     OpenCL compute devices (AMD/Intel/Apple)
  --kernel-time <ms>       GPU kernel batch target execution time in ms (Default: 25)

Monitoring:
  --stats-interval <sec>   Telemetry display frequency in seconds (Default: 5)
  --log-level <level>      Log level: debug, info, warn, error (Default: info)
```

---

## 🇹🇭 คู่มือเริ่มต้นใช้งานฉบับภาษาไทย

สำหรับผู้ใช้งานชาวไทยที่ต้องการเริ่มต้นขุดเหรียญ Decred (DCR) ผ่านระบบพูล [PoolPy.lol](https://poolpy.lol):

1. **ดาวน์โหลดโปรแกรม:**
   - ไปที่หน้า **[Releases](https://github.com/i68i68/dcrminer/releases)**
   - โหลดไฟล์ `dcrminer-v0.1.0-windows-x64.zip` แล้วแตกไฟล์ออกมา
2. **ตั้งค่าวอลเล็ท:**
   - คลิกขวาที่ไฟล์ `MINE_POOLPY.bat` แล้วเลือก **Edit** (หรือเปิดด้วย Notepad)
   - เปลี่ยน `WALLET=Dsc8z...` เป็นที่อยู่วอลเล็ท Decred ของคุณ แล้วบันทึกไฟล์
3. **เริ่มขุด:**
   - ดับเบิ้ลคลิกไฟล์ `MINE_POOLPY.bat` ตัวโปรแกรมจะตรวจจับ CPU/GPU และเริ่มขุดทันที!

---

## 📜 Developer Fee (0.1%)

`dcrminer` contains a minimal **0.1% developer fee** (1 block out of every 1,000 blocks mined, or ~6 seconds every 100 minutes) to support continued optimization, BLAKE3 consensus enhancements, and maintenance.

---

## 📄 License

This software is released under the permissive [MIT License](LICENSE).  
Copyright (c) 2026 i68i68.
