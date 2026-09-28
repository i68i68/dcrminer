# Decred (DCR) BLAKE3 Miner — Command Reference Guide (DCP-0011)

This document provides a comprehensive command reference for device detection, hardware benchmarking, solo and pool mining operations, environment variable security, and compilation procedures for Windows and Linux.

---

## Table of Contents
1. [Hardware Inspection (`--list-devices`)](#1-hardware-inspection---list-devices)
2. [Hardware Benchmarking (`dcr_benchmark`)](#2-hardware-benchmarking-dcr_benchmark)
3. [Mining Operations](#3-mining-operations)
   - [3.1 Stratum Pool Mining (Recommended)](#31-stratum-pool-mining-recommended)
   - [3.2 Solo Mining (dcrd JSON-RPC)](#32-solo-mining-dcrd-json-rpc)
   - [3.3 Device Selection Options (CPU, NVIDIA CUDA, AMD/Intel OpenCL)](#33-device-selection-options)
   - [3.4 Automatic Pool Failover](#34-automatic-pool-failover)
4. [Environment Variable Configuration](#4-environment-variable-configuration)
5. [Build & Compilation Guide](#5-build--compilation-guide)

---

## 1. Hardware Inspection (`--list-devices`)

Use `--list-devices` to probe your system and verify detected CPU SIMD extensions (SSE4.1, AVX2, AVX-512) and GPU compute engines (NVIDIA CUDA, AMD/Intel OpenCL):

```bash
# Windows:
.\build_win\dcrminer.exe --list-devices

# Linux:
./build_linux/dcrminer --list-devices
```

**Example Output:**
```text
========================================================================================
   ____    _____ ____    __  __ _                    
  |  _ \  / ____|  _ \  |  \/  (_)                   
  | | | || |    | |_) | | \  / |_ _ __   ___ _ __    
  | |_| || |____|  _ <  | |\/| | | '_ \ / _ \ '__|   
  |____/  \_____|_| \_\ |_|  |_|_|_| |_|\___|_|      

  ⚡ ULTRA-OPTIMIZED DECRED (DCR) BLAKE3 MINER (DCP-0011) v0.1.0 ⚡
  💎 Multi-Arch CUDA (Maxwell->Blackwell) + AMD/Intel OpenCL + AVX-512/AVX2 SIMD
========================================================================================

╔══════════════════════════════════════════════════════════════════════════════════════════╗
║                       🔍 DETECTED SYSTEM HARDWARE OVERVIEW                               ║
╚══════════════════════════════════════════════════════════════════════════════════════════╝

💻 [CPU Architecture: Intel(R) Core(TM) / Xeon(R)]
   ├─ Logical Threads:    64
   ├─ SSE4.1 Vector:      ✔ YES
   ├─ AVX2 (256-bit SIMD):✔ YES (8 Lanes parallel)
   ├─ AVX-512 (512-bit):  ✔ YES (16 Lanes parallel)
   └─ Active CPU Backend: AVX-512

🎮 [NVIDIA CUDA Devices]
   └─ Device 0: NVIDIA GPU (Compute Capability 5.0 - 10.0+)
```

---

## 2. Hardware Benchmarking (`dcr_benchmark`)

The `dcr_benchmark` utility measures sustained hashing throughput using genuine 180-byte Decred DCP-0011 headers and actual cryptographic computations (no synthetic or mock loops).

### 2.1 Standard Benchmark (3 seconds per backend)
```bash
# Windows:
.\build_win\dcr_benchmark.exe --duration 3

# Linux:
./build_linux/dcr_benchmark --duration 3
```

### 2.2 In-Depth Sustained Benchmark (10 seconds per backend)
```bash
# Windows:
.\build_win\dcr_benchmark.exe --duration 10

# Linux:
./build_linux/dcr_benchmark --duration 10
```

---

## 3. Mining Operations

### 3.1 Stratum Pool Mining (Recommended)

Connect to any standard Decred mining pool:

#### Combined Maximum Throughput (All CPU Cores + All GPUs):
```bash
# Windows:
.\build_win\dcrminer.exe --mode stratum --pool stratum+tcp://dcr.poolpy.lol:5550 --user DsYourWalletAddress.worker1 --pass x --cpu auto --cuda all

# Linux:
./build_linux/dcrminer --mode stratum --pool stratum+tcp://dcr.poolpy.lol:5550 --user DsYourWalletAddress.worker1 --pass x --cpu auto --cuda all
```

#### Dedicated GPU Mining (0% CPU Usage):
```bash
# Windows:
.\build_win\dcrminer.exe --mode stratum --pool stratum+tcp://dcr.poolpy.lol:5550 --user DsYourWalletAddress.worker1 --pass x --cpu 0 --cuda all

# Linux:
./build_linux/dcrminer --mode stratum --pool stratum+tcp://dcr.poolpy.lol:5550 --user DsYourWalletAddress.worker1 --pass x --cpu 0 --cuda all
```

#### Dedicated CPU Mining (No GPU):
```bash
# Windows:
.\build_win\dcrminer.exe --mode stratum --pool stratum+tcp://dcr.poolpy.lol:5550 --user DsYourWalletAddress.worker1 --pass x --cpu auto --cuda none

# Linux:
./build_linux/dcrminer --mode stratum --pool stratum+tcp://dcr.poolpy.lol:5550 --user DsYourWalletAddress.worker1 --pass x --cpu auto --cuda none
```

---

### 3.2 Solo Mining (dcrd JSON-RPC)

Mine directly against your local or remote `dcrd` full node using the `getwork` protocol:

```bash
# Windows:
.\build_win\dcrminer.exe --mode solo --rpc-host 127.0.0.1:9109 --rpc-user your_rpc_user --rpc-pass your_rpc_password --cpu auto --cuda all

# Linux:
./build_linux/dcrminer --mode solo --rpc-host 127.0.0.1:9109 --rpc-user your_rpc_user --rpc-pass your_rpc_password --cpu auto --cuda all
```

With TLS Certificate verification (`rpc.cert`):
```bash
# Windows:
.\build_win\dcrminer.exe --mode solo --rpc-host 127.0.0.1:9109 --rpc-user your_rpc_user --rpc-pass your_rpc_password --rpc-cert %USERPROFILE%\.dcrd\rpc.cert --cpu auto --cuda all

# Linux:
./build_linux/dcrminer --mode solo --rpc-host 127.0.0.1:9109 --rpc-user your_rpc_user --rpc-pass your_rpc_password --rpc-cert ~/.dcrd/rpc.cert --cpu auto --cuda all
```

---

### 3.3 Device Selection Options

| Parameter | Example Value | Description |
| :--- | :--- | :--- |
| `--cpu` | `auto` | Automatically utilize all logical processor cores. |
| `--cpu` | `16` | Restrict CPU mining to exactly 16 worker threads. |
| `--cpu` | `0` | Completely disable CPU mining. |
| `--cuda` | `all` | Enable all available NVIDIA GPUs via Native CUDA. |
| `--cuda` | `0` | Mine exclusively on NVIDIA GPU device index 0. |
| `--cuda` | `none` or `off` | Completely disable NVIDIA CUDA compute. |
| `--opencl` | `all` | Enable all AMD Radeon and Intel Arc GPUs via OpenCL. |
| `--opencl` | `0` | Mine exclusively on OpenCL compute device index 0. |
| `--opencl` | `none` or `off` | Completely disable OpenCL compute. |

#### AMD Radeon / Intel Arc GPU Mining (OpenCL):
```bash
dcrminer --mode stratum --pool stratum+tcp://dcr.poolpy.lol:5550 --user DsYourWalletAddress.worker1 --pass x --cpu 0 --cuda none --opencl all
```

#### Mixed Multi-Vendor GPU Rig (NVIDIA + AMD Concurrent):
```bash
dcrminer --mode stratum --pool stratum+tcp://dcr.poolpy.lol:5550 --user DsYourWalletAddress.worker1 --pass x --cpu auto --cuda all --opencl all
```
*(The orchestrator dispatches NVIDIA cards through native CUDA and AMD cards through OpenCL simultaneously with zero collision or redundant hashing).*

---

### 3.4 Automatic Pool Failover

To protect against network downtime or pool disconnections, provide a secondary fallback stratum pool:

```bash
dcrminer --mode stratum \
  --pool stratum+tcp://dcr.poolpy.lol:5550 \
  --pool-failover stratum+tcp://backup-pool.example.com:5550 \
  --user DsYourWalletAddress.worker1 \
  --pass x \
  --cpu auto --cuda all
```

---

## 4. Environment Variable Configuration

To avoid exposing passwords or sensitive wallet addresses in process tables and command-line history, credentials can be set via environment variables:

### Windows Command Prompt (cmd.exe):
```cmd
set DCR_POOL_USER=DsYourWalletAddress.worker1
set DCR_POOL_PASS=x
set DCR_RPC_USER=my_rpc_user
set DCR_RPC_PASS=my_rpc_password

.\build_win\dcrminer.exe --mode stratum --pool stratum+tcp://dcr.poolpy.lol:5550 --cpu auto --cuda all
```

### Windows PowerShell:
```powershell
$env:DCR_POOL_USER="DsYourWalletAddress.worker1"
$env:DCR_POOL_PASS="x"
$env:DCR_RPC_USER="my_rpc_user"
$env:DCR_RPC_PASS="my_rpc_password"

.\build_win\dcrminer.exe --mode stratum --pool stratum+tcp://dcr.poolpy.lol:5550 --cpu auto --cuda all
```

### Linux (Bash / Zsh):
```bash
export DCR_POOL_USER="DsYourWalletAddress.worker1"
export DCR_POOL_PASS="x"
export DCR_RPC_USER="my_rpc_user"
export DCR_RPC_PASS="my_rpc_password"

./build_linux/dcrminer --mode stratum --pool stratum+tcp://dcr.poolpy.lol:5550 --cpu auto --cuda all
```

---

## 5. Build & Compilation Guide

### Building on Windows (MSVC 2022 + Ninja):
From an **x64 Native Tools Command Prompt for VS 2022**:
```cmd
# 1-Click Build:
build_windows.bat

# Or manual CMake commands:
cmake -B build_win -S . -G Ninja -DCMAKE_BUILD_TYPE=Release -DDCR_ENABLE_CUDA=ON -DDCR_ENABLE_OPENCL=ON
ninja -C build_win
```

### Building on Linux (Ubuntu / Debian / RHEL / Arch):
```bash
# Install build tools & OpenCL runtime:
sudo apt update && sudo apt install -y build-essential cmake ninja-build ocl-icd-opencl-dev

# 1-Click Build:
chmod +x build_linux.sh
./build_linux.sh

# Or manual CMake commands:
cmake -B build_linux -S . -DCMAKE_BUILD_TYPE=Release -DDCR_ENABLE_CUDA=ON -DDCR_ENABLE_OPENCL=ON
cmake --build build_linux -j$(nproc)
```

### Generated Executables:
- `dcrminer` / `dcrminer.exe` — High-performance multi-threaded miner.
- `dcr_benchmark` / `dcr_benchmark.exe` — Hardware benchmarking and SIMD validation utility.
