#include "FileManipulator.h"
#include <filesystem>
#include <iostream>
#include <chrono>

namespace fs = std::filesystem;

namespace FileManipulator {

    bool BackupAndReplace(const std::string& downloadDir, const std::string& targetDir, const std::vector<std::string>& fileNames) {
        try {
            fs::path installPath(targetDir);
            fs::path downloadPath(downloadDir);

            // 1. Tạo thư mục backup với tên là thời gian hiện tại (VD: backup_20260306_173000)
            auto now = std::chrono::system_clock::now();
            auto time_t_now = std::chrono::system_clock::to_time_t(now);
            char timeStr[100];
            struct tm timeinfo;
            localtime_s(&timeinfo, &time_t_now);
            std::strftime(timeStr, sizeof(timeStr), "%Y%m%d_%H%M%S", &timeinfo);

            fs::path backupPath = installPath / "backups" / timeStr;
            fs::create_directories(backupPath);
            std::cout << "[*] Da tao thu muc backup: " << backupPath.string() << std::endl;

            // 2. Quét từng file để xử lý
            for (const auto& fileName : fileNames) {
                fs::path targetFile = installPath / fileName;
                fs::path newFile = downloadPath / fileName;
                fs::path backupFile = backupPath / fileName;
                // ===== [MA THUẬT NẰM Ở 2 DÒNG NÀY] =====
                // Ép C++ phải tạo toàn bộ các thư mục cha (nếu chưa có) trước khi copy!
                fs::create_directories(backupFile.parent_path());
                fs::create_directories(targetFile.parent_path());

                // Bước 2.1: Backup file cũ (Nếu file cũ đang tồn tại)
                if (fs::exists(targetFile)) {
                    std::cout << "[*] Dang backup: " << fileName << "..." << std::endl;
                    // Copy file cũ sang thư mục backup
                    fs::copy_file(targetFile, backupFile, fs::copy_options::overwrite_existing);
                }

                // Bước 2.2: Lôi file mới đè vào
                if (fs::exists(newFile)) {
                    std::cout << "[*] Dang chep de ban moi: " << fileName << "..." << std::endl;
                    // Lệnh này xả đè thẳng tay không thương tiếc
                    fs::copy_file(newFile, targetFile, fs::copy_options::overwrite_existing);
                }
                else {
                    std::cerr << "[-] Khong tim thay file tai ve: " << newFile.string() << std::endl;
                    return false;
                }
            }
            return true;
        }
        catch (const fs::filesystem_error& e) {
            std::cerr << "[-] Loi thao tac file (Co the do file con dang chay chua tat han): " << e.what() << std::endl;
            return false;
        }
    }
}