#ifndef FUSION_BATCH_STATUSMESSENGER_H
#define FUSION_BATCH_STATUSMESSENGER_H

#include <string>
#include <fstream>
#include <mutex>
#include <filesystem>
#include <iostream>

namespace fusion::batch {

class StatusMessenger {
public:
    static StatusMessenger& Instance() {
        static StatusMessenger instance;
        return instance;
    }

    void SetLogFile(const std::filesystem::path& logPath) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_logPath = logPath;
        if (!m_logPath.empty()) {
            std::filesystem::create_directories(m_logPath.parent_path());
        }
    }

    void SendStatus(const std::string& statusMessage, bool quiet = false) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!quiet) {
            std::cout << "[STATUS] " << statusMessage << "\n";
        }
        if (!m_logPath.empty()) {
            std::ofstream log(m_logPath, std::ios::app);
            if (log.is_open()) {
                log << statusMessage << "\n";
            }
        }
    }

    void SendProgress(const std::string& jobName, int current, int total) {
        int percent = (total > 0) ? (current * 100 / total) : 0;
        std::string msg = jobName + " Progress: " + std::to_string(current) + "/" + std::to_string(total) + " (" + std::to_string(percent) + "%)";
        SendStatus(msg);
    }

private:
    StatusMessenger() = default;
    ~StatusMessenger() = default;
    StatusMessenger(const StatusMessenger&) = delete;
    StatusMessenger& operator=(const StatusMessenger&) = delete;

    std::filesystem::path m_logPath;
    std::mutex m_mutex;
};

} // namespace fusion::batch

#endif // FUSION_BATCH_STATUSMESSENGER_H
