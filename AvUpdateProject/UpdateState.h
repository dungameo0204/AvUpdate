#pragma once
#include <string>
#include <mutex>
#include <atomic>
#include "json.hpp"

namespace UpdateState {
    // std::atomic giúp biến này luân chuyển giữa các luồng mà KHÔNG BAO GIỜ bị Crash
    extern std::atomic<bool> IS_UPDATING;
    extern std::atomic<double> PROCESS_DOWNLOAD;

    // std::string không support atomic, nên phải dùng Cảnh sát (Mutex) bảo vệ
    extern std::string CURRENT_STATUS_TEXT;
    extern std::mutex stateMutex;

    // Các hàm giao tiếp an toàn
    void SetStatus(double progress, const std::string& text);
    std::string GetStatusJson();
}