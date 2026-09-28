#pragma once

#include <string>
#include <vector>

namespace tuning {

struct DeviceProfile {
    int device_id;
    std::string backend;
    int optimal_threads;
    int optimal_block_size;
    int optimal_kernel_time;
    double max_hashrate;
};

class Autotuner {
public:
    Autotuner();
    ~Autotuner();

    void run_full_scan();
    void retune_device(int device_id, const std::string& backend);

    DeviceProfile get_profile(int device_id, const std::string& backend) const;
    void save_profiles(const std::string& directory) const;
    void load_profiles(const std::string& directory);

private:
    void tune_cpu();
    void tune_gpu(int device_id, const std::string& backend);
    
    std::vector<DeviceProfile> m_profiles;
};

} // namespace tuning
