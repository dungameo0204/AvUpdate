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

        // 1. TÌM VỊ TRÍ TUYỆT ĐỐI CỦA FILE .EXE
        wchar_t buffer[MAX_PATH];
        GetModuleFileNameW(NULL, buffer, MAX_PATH);
        fs::path exePath(buffer);

        // 2. TÌM FILE CONFIG NẰM CẠNH FILE .EXE
        fs::path configFilePath = exePath.parent_path() / "config.json";

        // 3. NẾU KHÔNG CÓ FILE -> TỰ ĐỘNG TẠO MỚI KÈM TRƯỜNG TARGET_INSTALL_DIR
        if (!fs::exists(configFilePath)) {
            std::cout << "[!] Khong tim thay config.json, he thong dang tao file mau..." << std::endl;
            json defaultCfg = {
                {"server_domain", "raw.githubusercontent.com"},
                {"api_path", "/dungameo0204/AvServer/main/AvServer/update_controller.json?v=1"},
                {"download_folder", "updates/downloads"}
                
            };
            std::ofstream outFile(configFilePath);
            outFile << defaultCfg.dump(4);
            outFile.close();
        }

        // 4. ĐỌC FILE VÀ BÓC TÁCH DỮ LIỆU
        try {
            std::ifstream inFile(configFilePath);
            json cfgJson;
            inFile >> cfgJson;

            config.server_domain = ConvertToWString(cfgJson["server_domain"]);
            config.api_path = ConvertToWString(cfgJson["api_path"]);
            if (cfgJson.contains("download_base_path")) {
                config.download_base_path = cfgJson["download_base_path"];
            }
            else {
                config.download_base_path = "/dungameo0204/AvServer/main/AvServer/"; // Fallback
            }
            config.download_folder = cfgJson["download_folder"];

            // ĐỌC TRƯỜNG DỮ LIỆU MỚI:
            if (cfgJson.contains("target_install_dir")) {
                config.target_install_dir = cfgJson["target_install_dir"];
            }
            else {
                // Đề phòng trường hợp file cũ của khách hàng chưa có trường này
                config.target_install_dir = "D:\\ProjectTraining\\AvScanVirus";
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[-] Loi doc file config.json: " << e.what() << std::endl;
            throw;
        }

        return config;
    }
}