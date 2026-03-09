#include "UpdateState.h"

namespace UpdateState {
    std::atomic<bool> IS_UPDATING{ false };
    std::atomic<double> PROCESS_DOWNLOAD{ 0.0 };
    std::string CURRENT_STATUS_TEXT = "Idle";
    std::mutex stateMutex;

    void SetStatus(double progress, const std::string& text) {
        std::lock_guard<std::mutex> lock(stateMutex); // Khóa cửa lại, cấm luồng khác chui vào
        PROCESS_DOWNLOAD = progress;
        CURRENT_STATUS_TEXT = text;
        // Hết hàm tự động mở khóa (Unlock)
    }

    std::string GetStatusJson() {
        std::lock_guard<std::mutex> lock(stateMutex);
        nlohmann::json j;
        j["is_updating"] = IS_UPDATING.load();
        j["progress"] = PROCESS_DOWNLOAD.load();
        j["message"] = CURRENT_STATUS_TEXT;
        return j.dump();
    }
}