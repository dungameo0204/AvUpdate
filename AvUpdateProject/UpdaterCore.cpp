#include "UpdaterCore.h"
#include <iostream>
#include <thread>
#include <fstream>
#include <chrono>
#include "Downloader.h"
#include "ConfigManager.h"
#include "UpdateValidator.h"
#include "AsyncEngine.h" // Triệu hồi sức mạnh của Động cơ

namespace UpdaterCore {

    bool ExecuteUpdate() {
        std::cout << "\n[=== BAT DAU TIEN TRINH CAP NHAT (ASYNC ENGINE) ===]" << std::endl;
        try {
            ConfigManager::AppConfig cfg = ConfigManager::LoadConfig();
            // =================================================================
            // MA THUẬT TIÊU DIỆT CACHE CDN (Gắn Timestamp vào URL)
            // =================================================================
            auto now = std::chrono::system_clock::now();
            auto ms_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

            std::wstring dynamicApiPath = cfg.api_path;
            // Kiểm tra xem trong config có dấu '?' chưa để nối chuỗi cho chuẩn
            if (dynamicApiPath.find('?') != std::string::npos) {
                dynamicApiPath += L"&t=" + std::to_wstring(ms_timestamp);
            }
            else {
                dynamicApiPath += L"?t=" + std::to_wstring(ms_timestamp);
            }
            // =================================================================

            // 1. Lễ tân check AV
            if (UpdateValidator::IsAvScanning()) {
                std::cout << "[-] AV dang ban quet, tu choi update!" << std::endl;
                return false;
            }

            // 2. Lễ tân lấy thông tin từ Server (Dùng cái dynamicApiPath vừa chế)
            models::UpdateCheckResponse response = Downloader::CheckUpdate(cfg.server_domain, dynamicApiPath);
            if (!response.has_update) return false;

            // 3. Lễ tân check Hạn sử dụng và Version
            std::string currentLocalVersion = "1.0.0";
            std::string versionFilePath = "D:\\ProjectTraining\\AvScanVirus\\version.txt";
            if(std::filesystem::exists(versionFilePath)) {
                std::ifstream verFile(versionFilePath);
                if (verFile.is_open()) {
                    std::getline(verFile, currentLocalVersion); // Hút dòng chữ version vào biến
                    verFile.close();
                }
            }
            std::cout << "[*] Phien ban hien tai duoi may la: " << currentLocalVersion << std::endl;
            // =========================================================
            if (!UpdateValidator::IsNewerVersion(response.manifest.version, currentLocalVersion)) return false;
            if (UpdateValidator::IsExpired(response.manifest.expires_at)) return false;
  
            // 4. KÍCH HOẠT ĐỘNG CƠ BÊN NGOÀI
            std::cout << "[+] Tham dinh an toan HOAN HAO. Khoi dong luong tai ngam..." << std::endl;

            // Gọi thằng AsyncEngine ra làm việc trong luồng riêng
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