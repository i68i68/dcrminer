#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <cstdint>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace dcr::telemetry {

struct GpuTelemetry {
    std::string name = "NVIDIA GPU";
    float temp_c = 0.0f;
    float power_w = 0.0f;
    int fan_pct = 0;
    bool valid = false;
};

class NvmlMonitor {
private:
    using nvmlReturn_t = int;
    using nvmlDevice_t = void*;

    using fn_nvmlInit = nvmlReturn_t (*)();
    using fn_nvmlShutdown = nvmlReturn_t (*)();
    using fn_nvmlDeviceGetCount = nvmlReturn_t (*)(unsigned int*);
    using fn_nvmlDeviceGetHandleByIndex = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
    using fn_nvmlDeviceGetName = nvmlReturn_t (*)(nvmlDevice_t, char*, unsigned int);
    using fn_nvmlDeviceGetTemperature = nvmlReturn_t (*)(nvmlDevice_t, int, unsigned int*);
    using fn_nvmlDeviceGetPowerUsage = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*);
    using fn_nvmlDeviceGetFanSpeed = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*);

#ifdef _WIN32
    HMODULE lib_handle_ = nullptr;
#else
    void* lib_handle_ = nullptr;
#endif

    fn_nvmlInit p_nvmlInit_ = nullptr;
    fn_nvmlShutdown p_nvmlShutdown_ = nullptr;
    fn_nvmlDeviceGetCount p_nvmlDeviceGetCount_ = nullptr;
    fn_nvmlDeviceGetHandleByIndex p_nvmlDeviceGetHandleByIndex_ = nullptr;
    fn_nvmlDeviceGetName p_nvmlDeviceGetName_ = nullptr;
    fn_nvmlDeviceGetTemperature p_nvmlDeviceGetTemperature_ = nullptr;
    fn_nvmlDeviceGetPowerUsage p_nvmlDeviceGetPowerUsage_ = nullptr;
    fn_nvmlDeviceGetFanSpeed p_nvmlDeviceGetFanSpeed_ = nullptr;

    bool initialized_ = false;
    unsigned int device_count_ = 0;
    std::vector<nvmlDevice_t> handles_;
    std::vector<std::string> names_;

public:
    NvmlMonitor() = default;

    ~NvmlMonitor() {
        shutdown();
    }

    bool init() {
        if (initialized_) return true;

#ifdef _WIN32
        lib_handle_ = LoadLibraryA("nvml.dll");
        if (!lib_handle_) {
            // Check Windows System32 explicit path
            lib_handle_ = LoadLibraryA("C:\\Windows\\System32\\nvml.dll");
        }
        if (!lib_handle_) return false;

        auto get_proc = [this](const char* name) -> FARPROC {
            return GetProcAddress(lib_handle_, name);
        };
#else
        lib_handle_ = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
        if (!lib_handle_) lib_handle_ = dlopen("libnvidia-ml.so", RTLD_LAZY);
        if (!lib_handle_) return false;

        auto get_proc = [this](const char* name) -> void* {
            return dlsym(lib_handle_, name);
        };
#endif

        p_nvmlInit_ = reinterpret_cast<fn_nvmlInit>(get_proc("nvmlInit_v2"));
        if (!p_nvmlInit_) p_nvmlInit_ = reinterpret_cast<fn_nvmlInit>(get_proc("nvmlInit"));

        p_nvmlShutdown_ = reinterpret_cast<fn_nvmlShutdown>(get_proc("nvmlShutdown"));

        p_nvmlDeviceGetCount_ = reinterpret_cast<fn_nvmlDeviceGetCount>(get_proc("nvmlDeviceGetCount_v2"));
        if (!p_nvmlDeviceGetCount_) p_nvmlDeviceGetCount_ = reinterpret_cast<fn_nvmlDeviceGetCount>(get_proc("nvmlDeviceGetCount"));

        p_nvmlDeviceGetHandleByIndex_ = reinterpret_cast<fn_nvmlDeviceGetHandleByIndex>(get_proc("nvmlDeviceGetHandleByIndex_v2"));
        if (!p_nvmlDeviceGetHandleByIndex_) p_nvmlDeviceGetHandleByIndex_ = reinterpret_cast<fn_nvmlDeviceGetHandleByIndex>(get_proc("nvmlDeviceGetHandleByIndex"));

        p_nvmlDeviceGetName_ = reinterpret_cast<fn_nvmlDeviceGetName>(get_proc("nvmlDeviceGetName"));
        p_nvmlDeviceGetTemperature_ = reinterpret_cast<fn_nvmlDeviceGetTemperature>(get_proc("nvmlDeviceGetTemperature"));
        p_nvmlDeviceGetPowerUsage_ = reinterpret_cast<fn_nvmlDeviceGetPowerUsage>(get_proc("nvmlDeviceGetPowerUsage"));
        p_nvmlDeviceGetFanSpeed_ = reinterpret_cast<fn_nvmlDeviceGetFanSpeed>(get_proc("nvmlDeviceGetFanSpeed"));

        if (!p_nvmlInit_ || !p_nvmlDeviceGetHandleByIndex_ || !p_nvmlDeviceGetTemperature_ || !p_nvmlDeviceGetPowerUsage_) {
            shutdown();
            return false;
        }

        if (p_nvmlInit_() != 0) { // 0 = NVML_SUCCESS
            shutdown();
            return false;
        }

        if (p_nvmlDeviceGetCount_) {
            p_nvmlDeviceGetCount_(&device_count_);
        }

        handles_.resize(device_count_);
        names_.resize(device_count_);

        for (unsigned int i = 0; i < device_count_; ++i) {
            nvmlDevice_t dev = nullptr;
            if (p_nvmlDeviceGetHandleByIndex_(i, &dev) == 0 && dev) {
                handles_[i] = dev;
                char name_buf[128] = {0};
                if (p_nvmlDeviceGetName_ && p_nvmlDeviceGetName_(dev, name_buf, sizeof(name_buf)) == 0) {
                    names_[i] = name_buf;
                } else {
                    names_[i] = "NVIDIA GPU #" + std::to_string(i);
                }
            }
        }

        initialized_ = true;
        return true;
    }

    void shutdown() {
        if (initialized_ && p_nvmlShutdown_) {
            p_nvmlShutdown_();
        }
        initialized_ = false;
        handles_.clear();
        names_.clear();
        device_count_ = 0;

        if (lib_handle_) {
#ifdef _WIN32
            FreeLibrary(lib_handle_);
#else
            dlclose(lib_handle_);
#endif
            lib_handle_ = nullptr;
        }
    }

    bool is_available() const {
        return initialized_;
    }

    unsigned int device_count() const {
        return device_count_;
    }

    std::string get_device_name(unsigned int idx) const {
        if (idx < names_.size() && !names_[idx].empty()) {
            return names_[idx];
        }
        return "NVIDIA GPU #" + std::to_string(idx);
    }

    GpuTelemetry get_telemetry(unsigned int idx) {
        GpuTelemetry tel;
        if (!initialized_ || idx >= handles_.size() || !handles_[idx]) {
            return tel;
        }

        nvmlDevice_t dev = handles_[idx];
        tel.name = get_device_name(idx);

        // 0 = NVML_TEMPERATURE_GPU
        unsigned int temp = 0;
        if (p_nvmlDeviceGetTemperature_ && p_nvmlDeviceGetTemperature_(dev, 0, &temp) == 0) {
            tel.temp_c = static_cast<float>(temp);
            tel.valid = true;
        }

        unsigned int mw = 0;
        if (p_nvmlDeviceGetPowerUsage_ && p_nvmlDeviceGetPowerUsage_(dev, &mw) == 0) {
            tel.power_w = static_cast<float>(mw) / 1000.0f;
            tel.valid = true;
        }

        unsigned int fan = 0;
        if (p_nvmlDeviceGetFanSpeed_ && p_nvmlDeviceGetFanSpeed_(dev, &fan) == 0) {
            tel.fan_pct = static_cast<int>(fan);
        }

        return tel;
    }
};

} // namespace dcr::telemetry
