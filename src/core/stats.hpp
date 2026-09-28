#pragma once

#include <atomic>
#include <cstdint>
#include <chrono>
#include <vector>
#include <string>
#include <cstdio>
#include <memory>
#include <iomanip>
#include <sstream>
#include <iostream>

#include "../common/terminal.hpp"

namespace dcr {

struct DeviceStats {
    std::string name;
    std::string type_label = "GPU"; // "GPU", "CPU"
    int hw_index = 0;
    std::atomic<uint64_t> hashes{0};
    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> rejected{0};
    std::atomic<uint64_t> stale{0};
    std::atomic<uint64_t> hw_errors{0};

    // Telemetry fields
    std::atomic<float> temp_c{0.0f};
    std::atomic<float> power_w{0.0f};
    std::atomic<int> fan_pct{0};
    std::atomic<bool> has_temp{false};
    std::atomic<bool> has_power{false};
    std::atomic<bool> has_fan{false};

    // For hashrate calculation
    std::atomic<uint64_t> last_hashes{0};
    std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();
    std::atomic<double> current_hashrate{0.0};
};

class MinerStats {
private:
    std::vector<std::unique_ptr<DeviceStats>> devices_;
    std::chrono::steady_clock::time_point start_time_ = std::chrono::steady_clock::now();
    double peak_hashrate_ = 0.0;

public:
    void clear() {
        devices_.clear();
    }

    void add_device(const std::string& name = "", const std::string& type_label = "GPU", int hw_index = 0) {
        auto dev = std::make_unique<DeviceStats>();
        dev->name = name.empty() ? ("Device " + std::to_string(devices_.size())) : name;
        dev->type_label = type_label;
        dev->hw_index = hw_index;
        devices_.push_back(std::move(dev));
    }

    void set_device_name(size_t index, const std::string& name) {
        if (index < devices_.size()) {
            devices_[index]->name = name;
        }
    }

    void update_telemetry(size_t index, float temp, float power, int fan, bool has_t, bool has_p, bool has_f) {
        if (index < devices_.size()) {
            devices_[index]->temp_c.store(temp, std::memory_order_relaxed);
            devices_[index]->power_w.store(power, std::memory_order_relaxed);
            devices_[index]->fan_pct.store(fan, std::memory_order_relaxed);
            devices_[index]->has_temp.store(has_t, std::memory_order_relaxed);
            devices_[index]->has_power.store(has_p, std::memory_order_relaxed);
            devices_[index]->has_fan.store(has_f, std::memory_order_relaxed);
        }
    }

    DeviceStats* get_device(size_t index) {
        if (index < devices_.size()) {
            return devices_[index].get();
        }
        return nullptr;
    }

    const DeviceStats* get_device(size_t index) const {
        if (index < devices_.size()) {
            return devices_[index].get();
        }
        return nullptr;
    }

    size_t num_devices() const {
        return devices_.size();
    }

    void update() {
        auto now = std::chrono::steady_clock::now();
        double current_total = 0.0;
        for (auto& dev : devices_) {
            uint64_t current_h = dev->hashes.load(std::memory_order_relaxed);
            uint64_t last_h = dev->last_hashes.exchange(current_h, std::memory_order_relaxed);
            double elapsed = std::chrono::duration<double>(now - dev->last_time).count();
            if (elapsed > 0) {
                double hr = static_cast<double>(current_h - last_h) / elapsed;
                double current = dev->current_hashrate.load(std::memory_order_relaxed);
                double smoothed = (current == 0.0) ? hr : (current * 0.4 + hr * 0.6);
                dev->current_hashrate.store(smoothed, std::memory_order_relaxed);
                current_total += smoothed;
            }
            dev->last_time = now;
        }
        if (current_total > peak_hashrate_) {
            peak_hashrate_ = current_total;
        }
    }

    double total_hashrate() const {
        double total = 0;
        for (const auto& dev : devices_) {
            total += dev->current_hashrate.load(std::memory_order_relaxed);
        }
        return total;
    }

    double peak_hashrate() const {
        return peak_hashrate_;
    }

    uint64_t total_accepted() const {
        uint64_t acc = 0;
        for (const auto& dev : devices_) {
            acc += dev->accepted.load(std::memory_order_relaxed);
        }
        return acc;
    }

    uint64_t total_rejected() const {
        uint64_t rej = 0;
        for (const auto& dev : devices_) {
            rej += dev->rejected.load(std::memory_order_relaxed);
        }
        return rej;
    }

    uint64_t total_hashes() const {
        uint64_t h = 0;
        for (const auto& dev : devices_) {
            h += dev->hashes.load(std::memory_order_relaxed);
        }
        return h;
    }

    std::string format_uptime() const {
        auto now = std::chrono::steady_clock::now();
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
        int h = static_cast<int>(secs / 3600);
        int m = static_cast<int>((secs % 3600) / 60);
        int s = static_cast<int>(secs % 60);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%02dh %02dm %02ds", h, m, s);
        return std::string(buf);
    }

    static std::string format_hashrate(double hr) {
        char buf[64];
        if (hr >= 1e12) {
            std::snprintf(buf, sizeof(buf), "%.2f TH/s", hr / 1e12);
        } else if (hr >= 1e9) {
            std::snprintf(buf, sizeof(buf), "%.2f GH/s", hr / 1e9);
        } else if (hr >= 1e6) {
            std::snprintf(buf, sizeof(buf), "%.2f MH/s", hr / 1e6);
        } else if (hr >= 1e3) {
            std::snprintf(buf, sizeof(buf), "%.2f KH/s", hr / 1e3);
        } else {
            std::snprintf(buf, sizeof(buf), "%.2f H/s", hr);
        }
        return std::string(buf);
    }

    static std::string format_number(uint64_t val) {
        std::string s = std::to_string(val);
        int n = static_cast<int>(s.length()) - 3;
        while (n > 0) {
            s.insert(n, ",");
            n -= 3;
        }
        return s;
    }

    void print_dashboard_line() const {
        double hr = total_hashrate();
        uint64_t acc = total_accepted();
        uint64_t rej = total_rejected();
        uint64_t total_shares = acc + rej;
        double ratio = (total_shares > 0) ? (static_cast<double>(acc) / total_shares * 100.0) : 100.0;

        float total_power = 0.0f;
        for (const auto& dev : devices_) {
            if (dev->has_power.load(std::memory_order_relaxed)) {
                total_power += dev->power_w.load(std::memory_order_relaxed);
            }
        }

        std::cout << term::DIM << term::get_timestamp() << term::RESET << " "
                  << term::BRIGHT_MAGENTA << term::BOLD << "⚡ HASHRATE:" << term::RESET << " "
                  << term::BRIGHT_GREEN << term::BOLD << std::setw(11) << std::left << format_hashrate(hr) << term::RESET
                  << term::DIM << " | " << term::RESET
                  << term::BRIGHT_YELLOW << "💎 Shares: " << term::RESET
                  << term::BRIGHT_GREEN << acc << term::RESET << "/"
                  << (rej > 0 ? term::BRIGHT_RED : term::DIM) << rej << term::RESET
                  << term::DIM << " (" << std::fixed << std::setprecision(1) << ratio << "%)" << term::RESET
                  << term::DIM << " | " << term::RESET
                  << term::BRIGHT_CYAN << "⏱️ Uptime: " << term::RESET << format_uptime()
                  << term::DIM << " | " << term::RESET
                  << term::BRIGHT_WHITE << "🔥 Peak: " << term::RESET << format_hashrate(peak_hashrate_);

        if (total_power > 0.0f) {
            double eff_mhw = (total_power > 0.0f) ? ((hr / 1e6) / total_power) : 0.0;
            std::cout << term::DIM << " | " << term::RESET
                      << term::BRIGHT_YELLOW << "⚡ Power: " << term::RESET
                      << term::BRIGHT_WHITE << std::fixed << std::setprecision(1) << total_power << "W" << term::RESET;
            if (hr > 0.0) {
                std::cout << term::DIM << " (" << std::fixed << std::setprecision(1) << eff_mhw << " MH/W)" << term::RESET;
            }
        }
        std::cout << "\n";

        // Print per-device telemetry breakdown
        for (size_t i = 0; i < devices_.size(); ++i) {
            const auto& dev = devices_[i];
            bool is_last = (i == devices_.size() - 1);
            std::string branch = is_last ? "  └─ " : "  ├─ ";
            double dev_hr = dev->current_hashrate.load(std::memory_order_relaxed);
            float temp = dev->temp_c.load(std::memory_order_relaxed);
            float pwr = dev->power_w.load(std::memory_order_relaxed);
            int fan = dev->fan_pct.load(std::memory_order_relaxed);

            std::string icon = (dev->type_label == "CPU") ? "💻" : "🎮";

            std::cout << term::DIM << branch << term::RESET
                      << icon << " " << term::BOLD << dev->name << term::RESET << ": "
                      << term::BRIGHT_GREEN << std::setw(11) << std::left << format_hashrate(dev_hr) << term::RESET;

            if (dev->has_temp.load(std::memory_order_relaxed)) {
                std::cout << term::DIM << " | " << term::RESET
                          << (temp >= 80.0f ? term::BRIGHT_RED : (temp >= 70.0f ? term::BRIGHT_YELLOW : term::BRIGHT_CYAN))
                          << "🌡️ " << std::fixed << std::setprecision(0) << temp << "°C" << term::RESET;
            }
            if (dev->has_power.load(std::memory_order_relaxed)) {
                std::cout << term::DIM << " | " << term::RESET
                          << term::BRIGHT_YELLOW << "⚡ " << std::fixed << std::setprecision(1) << pwr << "W" << term::RESET;
            }
            if (dev->has_fan.load(std::memory_order_relaxed)) {
                std::cout << term::DIM << " | " << term::RESET
                          << term::BRIGHT_WHITE << "💨 Fan: " << fan << "%" << term::RESET;
            }
            if (pwr > 0.0f && dev_hr > 0.0) {
                double eff = (dev_hr / 1e6) / pwr;
                std::cout << term::DIM << " | " << term::RESET
                          << term::BRIGHT_GREEN << "💡 " << std::fixed << std::setprecision(1) << eff << " MH/W" << term::RESET;
            }
            std::cout << "\n";
        }
    }
};

} // namespace dcr
