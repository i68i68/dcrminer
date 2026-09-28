#pragma once

#include "nvml_monitor.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <cmath>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "../blake3/cpu_features.hpp"

namespace dcr::telemetry {

struct DeviceTelemetryInfo {
    std::string name;
    std::string type_label; // "GPU", "CPU"
    int hw_id = 0;
    float temp_c = 0.0f;
    float power_w = 0.0f;
    int fan_pct = 0;
    bool has_temp = false;
    bool has_power = false;
    bool has_fan = false;
};

class HardwareMonitor {
private:
    NvmlMonitor nvml_;

#ifdef _WIN32
    FILETIME prev_idle_time_{0, 0};
    FILETIME prev_kernel_time_{0, 0};
    FILETIME prev_user_time_{0, 0};
    float cpu_usage_pct_ = 0.0f;
    std::chrono::steady_clock::time_point last_cpu_check_ = std::chrono::steady_clock::now();
#endif

public:
    HardwareMonitor() {
        nvml_.init();
#ifdef _WIN32
        GetSystemTimes(&prev_idle_time_, &prev_kernel_time_, &prev_user_time_);
#endif
    }

    ~HardwareMonitor() {
        nvml_.shutdown();
    }

    bool is_nvml_available() const {
        return nvml_.is_available();
    }

    DeviceTelemetryInfo get_gpu_telemetry(int gpu_id) {
        DeviceTelemetryInfo info;
        info.type_label = "GPU";
        info.hw_id = gpu_id;

        if (nvml_.is_available() && gpu_id < static_cast<int>(nvml_.device_count())) {
            auto tel = nvml_.get_telemetry(static_cast<unsigned int>(gpu_id));
            info.name = tel.name;
            info.temp_c = tel.temp_c;
            info.power_w = tel.power_w;
            info.fan_pct = tel.fan_pct;
            info.has_temp = (tel.temp_c > 0.0f);
            info.has_power = (tel.power_w > 0.0f);
            info.has_fan = (tel.fan_pct > 0);
        } else {
            info.name = "GPU #" + std::to_string(gpu_id);
        }
        return info;
    }

    DeviceTelemetryInfo get_cpu_telemetry(int active_threads, int total_threads) {
        DeviceTelemetryInfo info;
        info.name = blake3::get_cpu_brand_string();
        info.type_label = "CPU";
        info.hw_id = 0;

#ifdef _WIN32
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_cpu_check_).count() >= 500) {
            FILETIME idle_time, kernel_time, user_time;
            if (GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
                auto ft_to_u64 = [](const FILETIME& ft) -> uint64_t {
                    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
                };

                uint64_t idle = ft_to_u64(idle_time) - ft_to_u64(prev_idle_time_);
                uint64_t kernel = ft_to_u64(kernel_time) - ft_to_u64(prev_kernel_time_);
                uint64_t user = ft_to_u64(user_time) - ft_to_u64(prev_user_time_);
                uint64_t total = kernel + user;

                if (total > 0 && total >= idle) {
                    cpu_usage_pct_ = static_cast<float>(total - idle) * 100.0f / static_cast<float>(total);
                }

                prev_idle_time_ = idle_time;
                prev_kernel_time_ = kernel_time;
                prev_user_time_ = user_time;
                last_cpu_check_ = now;
            }
        }

        // Estimate power based on Xeon TDP (~205W) and active threads or CPU usage
        if (active_threads > 0) {
            float thread_ratio = static_cast<float>(active_threads) / static_cast<float>(std::max(1, total_threads));
            info.power_w = 25.0f + 165.0f * thread_ratio;
            info.has_power = true;
            // Estimated thermal under AVX-512 load
            info.temp_c = 42.0f + 33.0f * thread_ratio;
            info.has_temp = true;
        } else {
            // Idle power
            info.power_w = 18.5f + (cpu_usage_pct_ * 0.4f);
            info.has_power = true;
            info.has_temp = false;
        }
#endif
        return info;
    }
};

} // namespace dcr::telemetry
