#include "ConfigManager.h"
#include "json.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace ConfigManager {

    std::wstring ConvertToWString(const std::string& s) {
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
        wchar_t* buf = new wchar_t[len];
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, buf, len);
        std::wstring r(buf);
        delete[] buf;
        return r;
    }

    AppConfig LoadConfig() {
        AppConfig config;

        // 1. TÌM VỊ TRÍ TUYỆT ĐỐI CỦA FILE .EXE (Bulletproof method)
        wchar_t buffer[MAX_PATH];
        GetModuleFileNameW(NULL, buffer, MAX_PATH);
        fs::path exePath(buffer);

        // Lấy thư mục cha (ví dụ: C:\AvProject\Release)
        config.app_root_dir = exePath.parent_path().string();

        // 2. TÌM FILE CONFIG NẰM CẠNH FILE .EXE
        fs::path configFilePath = exePath.parent_path() / "config.json";

        // 3. NẾU KHÔNG CÓ FILE -> TỰ ĐỘNG TẠO MỚI (Fallback)
        if (!fs::exists(configFilePath)) {
            std::cout << "[!] Khong tim thay config.json, he thong dang tao file mau..." << std::endl;
            json defaultCfg = {
                {"server_domain", "dungameo0204.github.io"},
                {"api_path", "/AvServer/update_controller.json"},
                {"download_folder", "updates/downloads"} // Nâng cấp lưu hẳn vào thư mục con updates/
            };
            std::ofstream outFile(configFilePath);
            outFile << defaultCfg.dump(4); // dump(4) để căn lề JSON cho đẹp
            outFile.close();
        }

        // 4. ĐỌC FILE VÀ BÓC TÁCH DỮ LIỆU
        try {
            std::ifstream inFile(configFilePath);
            json cfgJson;
            inFile >> cfgJson;

            config.server_domain = ConvertToWString(cfgJson["server_domain"]);
            config.api_path = ConvertToWString(cfgJson["api_path"]);
            config.download_folder = cfgJson["download_folder"];
        }
        catch (const std::exception& e) {
            std::cerr << "[-] Loi doc file config.json: " << e.what() << std::endl;
            throw; // Quăng lỗi ra ngoài cho main xử lý
        }

        return config;
    }
}