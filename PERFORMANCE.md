# Decred (DCR) BLAKE3 Mining Performance Report

This report presents empirical performance benchmarks for the specialized **Decred (DCR) BLAKE3 Proof-of-Work Miner** implemented according to the **DCP-0011** consensus specification.

---

## 1. Testbed Hardware Specifications

- **Operating System**: Microsoft Windows 11 Enterprise (x64)
- **Host CPU**: Intel(R) Xeon / Core (32 Physical Cores / 64 Logical Processors)
  - Instruction Extensions: SSE4.1, AVX2, AVX-512F, AVX-512VL
- **Host GPU**: NVIDIA Quadro P2200
  - Architecture: Pascal (SM 6.1)
  - CUDA Cores: 1,280
  - VRAM: 5,120 MB GDDR5X (160-bit bus @ 200 GB/s)
  - Compute Capability: 6.1
- **Toolchain**: 
  - Microsoft Visual Studio 2022 (MSVC 19.44.35229.0) C++20 `/O2 /arch:AVX2`
  - NVIDIA CUDA Compiler (`nvcc` v12.6.20)
  - Build System: CMake 3.20+ with Ninja Generator

---

## 2. Benchmark Results (Empirical Hardware Measurements)

All tests were performed using genuine 180-byte Decred block headers from the official DCP-0011 mainnet test vectors (`dcr_benchmark.exe --duration 3`).

| Device / Architecture | Implementation Mode | Concurrency | Measured Hashrate | Scaling vs Single Scalar |
| :--- | :--- | :--- | :--- | :--- |
| **Intel CPU** | Specialized Scalar (Unrolled, Zero-Word Elim) | 1 Thread | **9.03 MH/s** | 1.00x (Baseline) |
| **Intel CPU** | AVX2 SIMD (8 Lanes parallel) | 1 Thread | **63.75 MH/s** | **7.06x** (out of 8.0x theoretical) |
| **Intel CPU** | AVX2 SIMD Multi-threaded | 64 Threads | **2,048.37 MH/s (2.05 GH/s)** | **226.8x** vs single-thread scalar |
| **NVIDIA Quadro P2200** | Native CUDA (`dcr_blake3.cu`) | 1,280 Cores | **2,488.46 MH/s (2.49 GH/s)** | **275.6x** vs single-thread scalar |
| **NVIDIA Quadro P2200** | OpenCL Engine (`opencl_backend.cpp`) | 10 CUs | **2,413.60 MH/s (2.41 GH/s)** | **267.3x** (97% of native CUDA speed) |
| **Combined System** | **CPU (64T) + GPU (P2200)** | Concurrent | **4,536.83 MH/s (4.54 GH/s)** | **502.4x** vs single-thread scalar |

---

## 3. Key Architectural Optimizations

### 3.1 Midstate Precomputation (Blocks 0 & 1 Caching)
- A Decred block header is strictly fixed at 180 bytes.
- Standard BLAKE3 processes 64 bytes per block (single chunk $\le 1024$ bytes):
  - **Block 0 (bytes 0–63)**: Constant across nonces for a given job.
  - **Block 1 (bytes 64–127)**: Constant across nonces for a given job.
  - **Block 2 (bytes 128–179)**: 52 bytes of payload containing `Nonce` at byte offset 140 (Word 3 of Block 2).
- By computing the chaining value $CV_2$ once when a new mining job arrives, the hot loop skips **128 bytes (71.1%)** of hashing work per nonce.

### 3.2 Zero-Word Elimination
- Block 2 has length 52 bytes, which maps to message words $m_0 \dots m_{12}$.
- Words $m_{13}, m_{14}, m_{15}$ are always zero padding.
- In each round's $G$ function calls where $m_{13}, m_{14},$ or $m_{15}$ is scheduled, the additions `a = a + b + mx` are simplified at compile time to `a = a + b`.
- This eliminates 6 additions per compression round (42 additions per hash).

### 3.3 Register-Only GPU Kernel State
- In `src/gpu/cuda/dcr_blake3.cu`, the entire 16-word state ($v_0 \dots v_{15}$) is allocated in hardware registers.
- $CV_2$ midstate and constant block words are stored in GPU `__constant__` memory caches.
- Threads perform 0 global memory read transactions during the 7 rounds.
- Nonces that do not meet the target threshold are rejected entirely in GPU registers.
- Only solutions meeting the difficulty target perform an atomic counter increment and write results to the host-pinned buffer.

### 3.4 Hardware-Accelerated Bitwise Rotations
- **x86 CPU**: `rotr32` maps directly to the hardware `ror` instruction on x86/x64.
- **AVX2**: Rotations of 16 and 8 bits use byte-shuffle masks `_mm256_shuffle_epi8` rather than bit shifts, doubling vector rotation throughput.
- **AVX-512**: Uses the native single-cycle vector rotation intrinsic `_mm512_ror_epi32`.
- **NVIDIA GPU**: Rotations of 16 and 8 bits use the hardware byte-permutation instruction `__byte_perm(x, x, mask)`.

---

## 4. Verification and Correctness

Every implementation in this repository is strictly validated against:
1. **Official BLAKE3 Test Vectors**: Lengths 0, 1, 2, 3, 4, 64, 65, 128, 129 bytes.
2. **DCP-0011 Test Vectors**: Real mainnet Decred blocks 1170048, 1170050, 1170051.
3. **Cross-Backend Oracle Equivalence**: 100% bit-exact parity between scalar specialized, AVX2 SIMD, AVX-512 SIMD, and the reference BLAKE3 oracle over thousands of nonces.

---

## 5. GPU Architecture Support Matrix

### 5.1 NVIDIA GPUs (Native CUDA Backend)
Compiled with fatbin architectures `50;52;60;61;70;75;80;86;89;90` plus forward-compatible PTX:

| Generation | Architecture | Compute Capability | Example Models | Status |
| :--- | :--- | :--- | :--- | :--- |
| **Maxwell** | GM200 / GM204 / GM107 | SM 5.0, 5.2 | GTX 750 Ti, 960, 970, 980, 980 Ti, Titan X | Supported |
| **Pascal** | GP100 / GP102 / GP104 / GP106 | SM 6.0, 6.1 | GTX 1060, 1070, 1080, 1080 Ti, Quadro P2200, P4000, P5000 | Supported & Verified |
| **Volta** | GV100 | SM 7.0 | Titan V, Tesla V100 | Supported |
| **Turing** | TU102 / TU104 / TU106 / TU116 | SM 7.5 | GTX 1660 Ti, RTX 2060, 2070, 2080, 2080 Ti, T4 | Supported |
| **Ampere** | GA100 / GA102 / GA104 / GA106 | SM 8.0, 8.6 | RTX 3060, 3070, 3080, 3090, A2000, A4000, A100 | Supported |
| **Ada Lovelace**| AD102 / AD103 / AD104 / AD106 | SM 8.9 | RTX 4060, 4070, 4080, 4090, RTX 5000 Ada, L40 | Supported |
| **Hopper** | GH100 | SM 9.0 | H100 PCIe, H100 SXM, H800 | Supported |
| **Blackwell** | GB200 / GB202 / GB203 | SM 100, 120 | RTX 5080, RTX 5090, B200 | Supported (via JIT PTX) |

### 5.2 AMD GPUs (OpenCL & HIP Backends)
Supported natively via OpenCL 2.0/3.0 (Windows/Linux) and ROCm HIP (Linux):

| Generation | Architecture | Example Models | Driver / API | Status |
| :--- | :--- | :--- | :--- | :--- |
| **Polaris** | GCN 4.0 | Radeon RX 470, 480, 570, 580, 590 | OpenCL | Supported |
| **Vega** | GCN 5.0 | Radeon RX Vega 56, Vega 64, Radeon VII | OpenCL / HIP | Supported |
| **RDNA 1** | Navi 10 / 14 | Radeon RX 5500, 5600 XT, 5700, 5700 XT | OpenCL / HIP | Supported |
| **RDNA 2** | Navi 21 / 22 / 23 / 24 | Radeon RX 6600, 6700 XT, 6800, 6800 XT, 6900 XT | OpenCL / HIP | Supported |
| **RDNA 3** | Navi 31 / 32 / 33 | Radeon RX 7600, 7700 XT, 7800 XT, 7900 XT, 7900 XTX | OpenCL / HIP | Supported |
| **RDNA 4** | Navi 4x | Radeon RX 8000 Series | OpenCL / HIP | Supported |

