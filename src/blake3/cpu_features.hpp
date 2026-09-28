#pragma once

#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

#include "../common/types.hpp"

namespace dcr {
namespace blake3 {

struct CpuFeatures {
    bool sse41;
    bool avx2;
    bool avx512f;
    bool avx512vl;
};

inline CpuFeatures detect_cpu_features() {
    CpuFeatures features = {false, false, false, false};
    int info[4] = {0};

#if defined(_MSC_VER)
    __cpuid(info, 0);
    int nIds = info[0];
    if (nIds >= 1) {
        __cpuid(info, 1);
        features.sse41 = (info[2] & (1 << 19)) != 0;
    }
    if (nIds >= 7) {
        __cpuidex(info, 7, 0);
        features.avx2 = (info[1] & (1 << 5)) != 0;
        features.avx512f = (info[1] & (1 << 16)) != 0;
        features.avx512vl = (info[1] & (1 << 31)) != 0;
    }
#else
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        features.sse41 = (ecx & (1 << 19)) != 0;
    }
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        features.avx2 = (ebx & (1 << 5)) != 0;
        features.avx512f = (ebx & (1 << 16)) != 0;
        features.avx512vl = (ebx & (1 << 31)) != 0;
    }
#endif

    return features;
}

inline dcr::BackendType select_best_cpu_backend() {
    CpuFeatures features = detect_cpu_features();
    if (features.avx512f && features.avx512vl) {
        return dcr::BackendType::CPU_AVX512;
    }
    if (features.avx2) {
        return dcr::BackendType::CPU_AVX2;
    }
    if (features.sse41) {
        return dcr::BackendType::CPU_SSE41;
    }
    return dcr::BackendType::CPU_SCALAR;
}

inline std::string get_cpu_brand_string() {
    char brand[49] = {0};
    int info[4] = {0};
#if defined(_MSC_VER)
    __cpuid(info, static_cast<int>(0x80000000));
    unsigned int nExIds = static_cast<unsigned int>(info[0]);
    if (nExIds >= 0x80000004) {
        __cpuid(reinterpret_cast<int*>(brand), static_cast<int>(0x80000002));
        __cpuid(reinterpret_cast<int*>(brand + 16), static_cast<int>(0x80000003));
        __cpuid(reinterpret_cast<int*>(brand + 32), static_cast<int>(0x80000004));
    }
#else
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(0x80000000, &eax, &ebx, &ecx, &edx) && eax >= 0x80000004) {
        __get_cpuid(0x80000002, (unsigned int*)(brand), (unsigned int*)(brand + 4), (unsigned int*)(brand + 8), (unsigned int*)(brand + 12));
        __get_cpuid(0x80000003, (unsigned int*)(brand + 16), (unsigned int*)(brand + 20), (unsigned int*)(brand + 24), (unsigned int*)(brand + 28));
        __get_cpuid(0x80000004, (unsigned int*)(brand + 32), (unsigned int*)(brand + 36), (unsigned int*)(brand + 40), (unsigned int*)(brand + 44));
    }
#endif
    std::string s = brand;
    size_t first = s.find_first_not_of(" \t");
    if (first != std::string::npos) s = s.substr(first);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\0')) s.pop_back();
    return s.empty() ? "Generic x86-64 CPU" : s;
}

} // namespace blake3
} // namespace dcr
