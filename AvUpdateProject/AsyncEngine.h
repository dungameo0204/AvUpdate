#pragma once
#include "ConfigManager.h"
#include "models.h" // Hoặc tên file chứa struct models::UpdateCheckResponse của bác

namespace AsyncEngine {
    // Hàm này sẽ được ném vào std::thread
    void BackgroundUpdateTask(models::UpdateCheckResponse remoteData, ConfigManager::AppConfig cfg);
}