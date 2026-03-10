#pragma once
#include <string>

namespace ConfigManager {
    // Struct lưu trữ toàn bộ cấu hình lấy từ file
    struct AppConfig {
        std::wstring server_domain;
        std::wstring api_path;
        std::string download_base_path;
        std::string download_folder;
        std::string target_install_dir;
    };

    // Hàm load cấu hình
    AppConfig LoadConfig();

    // Hàm phụ trợ chuyển đổi chuỗi (Tôi chuyển từ main.cpp sang đây cho gọn gàng)
    std::wstring ConvertToWString(const std::string& s);
}