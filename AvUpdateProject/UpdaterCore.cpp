#include "UpdaterCore.h"
#include <iostream>
#include <thread>
#include <fstream>
#include <chrono>
#include "Downloader.h"
#include "ConfigManager.h"
#include "UpdateValidator.h"
#include "AsyncEngine.h"

namespace UpdaterCore {

    bool ExecuteUpdate() {
        std::cout << "\n[=== BAT DAU TIEN TRINH CAP NHAT (ASYNC ENGINE) ===]" << std::endl;
        try {
            ConfigManager::AppConfig cfg = ConfigManager::LoadConfig();

            // MA THUẬT TIÊU DIỆT CACHE CDN
            auto now = std::chrono::system_clock::now();
            auto ms_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            std::wstring dynamicApiPath = cfg.api_path;
            if (dynamicApiPath.find('?') != std::string::npos) {
                dynamicApiPath += L"&t=" + std::to_wstring(ms_timestamp);
            }
            else {
                dynamicApiPath += L"?t=" + std::to_wstring(ms_timestamp);
            }

            // 1. Lễ tân lấy thông tin từ Server
            models::UpdateCheckResponse response = Downloader::CheckUpdate(cfg.server_domain, dynamicApiPath);
            if (!response.has_update) return false;

            // 2. Lễ tân check Version
            std::string currentLocalVersion = "1.0.0";
            std::string versionFilePath = "D:\\ProjectTraining\\AvScanVirus\\version.txt";
            if (std::filesystem::exists(versionFilePath)) {
                std::ifstream verFile(versionFilePath);
                if (verFile.is_open()) {
                    std::getline(verFile, currentLocalVersion);
                    verFile.close();
                }
            }
            std::cout << "[*] Phien ban hien tai duoi may la: " << currentLocalVersion << std::endl;

            if (!UpdateValidator::IsNewerVersion(response.manifest.version, currentLocalVersion)) return false;
            if (UpdateValidator::IsExpired(response.manifest.expires_at)) return false;

            // 3. KÍCH HOẠT ĐỘNG CƠ BÊN NGOÀI (Chuyển hết gánh nặng chờ đợi sang đây)
            std::cout << "[+] Tham dinh an toan HOAN HAO. Khoi dong luong tai ngam..." << std::endl;

            std::thread asyncWorker(AsyncEngine::BackgroundUpdateTask, response, cfg);
            asyncWorker.detach();

            std::cout << "[+] Da giao viec cho dong co AsyncEngine. Tra ket qua som cho Client!" << std::endl;
            return true;
        }
        catch (const std::exception& e) {
            std::cerr << "[-] LOI CORE: " << e.what() << std::endl;
            return false;
        }
    }
}